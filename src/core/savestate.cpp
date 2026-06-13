// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <istream>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <utility>
#include <cryptopp/hex.h>
#include <fmt/ranges.h>
#ifdef __SWITCH__
#include <zstd.h>
#endif
#include "common/archives.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/scm_rev.h"
#ifdef __SWITCH__
#include "common/scope_exit.h"
#endif
#include "common/swap.h"
#include "common/zstd_compression.h"
#include "core/core.h"
#include "core/loader/loader.h"
#include "core/movie.h"
#include "core/savestate.h"
#include "core/savestate_data.h"
#include "network/network.h"

namespace Core {

#pragma pack(push, 1)
struct CSTHeader {
    std::array<u8, 4> filetype;    /// Unique Identifier to check the file type (always "CST"0x1B)
    u64_le program_id;             /// ID of the ROM being executed. Also called title_id
    std::array<u8, 20> revision;   /// Git hash of the revision this savestate was created with
    u64_le time;                   /// The time when this save state was created
    std::array<u8, 20> build_name; /// The build name (Canary/Nightly) with the version number
    u32_le zero = 0;               /// Should be zero, just in case.

    std::array<u8, 192> reserved{}; /// Make heading 256 bytes so it has consistent size
};
static_assert(sizeof(CSTHeader) == 256, "CSTHeader should be 256 bytes");
#pragma pack(pop)

constexpr std::array<u8, 4> header_magic_bytes{{'C', 'S', 'T', 0x1B}};
constexpr int save_state_compression_level = 1;

static std::vector<u8> CompressSaveStateData(std::span<const u8> data) {
#ifdef __SWITCH__
    return Common::Compression::CompressDataZSTD(data, save_state_compression_level);
#else
    return Common::Compression::CompressDataZSTDDefault(data);
#endif
}

#ifdef __SWITCH__
class ZstdSaveStateOutputStreamBuf final : public std::streambuf {
public:
    explicit ZstdSaveStateOutputStreamBuf(FileUtil::IOFile& file_) : file{file_} {
        stream = ZSTD_createCStream();
        if (!stream) {
            throw std::runtime_error("Failed to create ZSTD save state stream");
        }

        std::size_t result = ZSTD_CCtx_reset(stream, ZSTD_reset_session_only);
        if (ZSTD_isError(result)) {
            throw std::runtime_error(ZSTD_getErrorName(result));
        }

        result = ZSTD_CCtx_setParameter(stream, ZSTD_c_compressionLevel,
                                        save_state_compression_level);
        if (ZSTD_isError(result)) {
            throw std::runtime_error(ZSTD_getErrorName(result));
        }

        setp(input_buffer.data(), input_buffer.data() + input_buffer.size());
    }

    ~ZstdSaveStateOutputStreamBuf() override {
        if (stream) {
            ZSTD_freeCStream(stream);
        }
    }

    std::size_t GetInputBytes() const {
        return input_bytes;
    }

    bool Finish() {
        if (finished) {
            return true;
        }

        if (!CompressBufferedInput()) {
            return false;
        }

        for (;;) {
            ZSTD_outBuffer output{output_buffer.data(), output_buffer.size(), 0};
            const std::size_t result = ZSTD_endStream(stream, &output);
            if (ZSTD_isError(result)) {
                LOG_ERROR(Core, "Failed to finish save state stream: {}",
                          ZSTD_getErrorName(result));
                return false;
            }
            if (!WriteOutput(output)) {
                return false;
            }
            if (result == 0) {
                break;
            }
        }

        finished = true;
        return true;
    }

protected:
    int overflow(int ch) override {
        if (!CompressBufferedInput()) {
            return traits_type::eof();
        }
        if (!traits_type::eq_int_type(ch, traits_type::eof())) {
            *pptr() = traits_type::to_char_type(ch);
            pbump(1);
        }
        return traits_type::not_eof(ch);
    }

    std::streamsize xsputn(const char* data, std::streamsize count) override {
        std::streamsize written = 0;
        while (written < count) {
            auto available = static_cast<std::streamsize>(epptr() - pptr());
            if (available == 0) {
                if (!CompressBufferedInput()) {
                    break;
                }
                available = static_cast<std::streamsize>(epptr() - pptr());
            }

            const std::streamsize copy_size = std::min(count - written, available);
            std::memcpy(pptr(), data + written, static_cast<std::size_t>(copy_size));
            pbump(static_cast<int>(copy_size));
            written += copy_size;
        }
        return written;
    }

    int sync() override {
        return CompressBufferedInput() ? 0 : -1;
    }

private:
    bool CompressBufferedInput() {
        const auto pending = static_cast<std::size_t>(pptr() - pbase());
        if (pending == 0) {
            return true;
        }

        ZSTD_inBuffer input{input_buffer.data(), pending, 0};
        while (input.pos < input.size) {
            ZSTD_outBuffer output{output_buffer.data(), output_buffer.size(), 0};
            const std::size_t result = ZSTD_compressStream(stream, &output, &input);
            if (ZSTD_isError(result)) {
                LOG_ERROR(Core, "Failed to compress save state stream: {}",
                          ZSTD_getErrorName(result));
                return false;
            }
            if (!WriteOutput(output)) {
                return false;
            }
        }

        input_bytes += pending;
        setp(input_buffer.data(), input_buffer.data() + input_buffer.size());
        return true;
    }

    bool WriteOutput(const ZSTD_outBuffer& output) {
        if (output.pos == 0) {
            return true;
        }
        return file.WriteBytes(output_buffer.data(), output.pos) == output.pos;
    }

    FileUtil::IOFile& file;
    ZSTD_CStream* stream{};
    std::array<char, 64 * 1024> input_buffer{};
    std::array<u8, 64 * 1024> output_buffer{};
    std::size_t input_bytes{};
    bool finished{};
};

class ZstdSaveStateInputStreamBuf final : public std::streambuf {
public:
    ZstdSaveStateInputStreamBuf(FileUtil::IOFile& file_, std::size_t compressed_size_)
        : file{file_}, compressed_remaining{compressed_size_} {
        stream = ZSTD_createDStream();
        if (!stream) {
            throw std::runtime_error("Failed to create ZSTD load state stream");
        }

        const std::size_t result = ZSTD_initDStream(stream);
        if (ZSTD_isError(result)) {
            throw std::runtime_error(ZSTD_getErrorName(result));
        }

        setg(output_buffer.data(), output_buffer.data(), output_buffer.data());
    }

    ~ZstdSaveStateInputStreamBuf() override {
        if (stream) {
            ZSTD_freeDStream(stream);
        }
    }

protected:
    int underflow() override {
        if (gptr() < egptr()) {
            return traits_type::to_int_type(*gptr());
        }

        ZSTD_outBuffer output{output_buffer.data(), output_buffer.size(), 0};
        while (output.pos == 0) {
            if (input.pos == input.size) {
                if (compressed_remaining == 0) {
                    if (!frame_finished) {
                        throw std::runtime_error("Truncated compressed savestate");
                    }
                    return traits_type::eof();
                }

                const auto read_size =
                    std::min<std::size_t>(input_buffer.size(), compressed_remaining);
                const std::size_t read = file.ReadBytes(input_buffer.data(), read_size);
                if (read != read_size) {
                    throw std::runtime_error("Could not read compressed savestate");
                }
                compressed_remaining -= read;
                input = ZSTD_inBuffer{input_buffer.data(), read, 0};
            }

            const std::size_t result = ZSTD_decompressStream(stream, &output, &input);
            if (ZSTD_isError(result)) {
                throw std::runtime_error(ZSTD_getErrorName(result));
            }
            if (result == 0) {
                frame_finished = true;
            }
        }

        setg(output_buffer.data(), output_buffer.data(), output_buffer.data() + output.pos);
        return traits_type::to_int_type(*gptr());
    }

    std::streamsize xsgetn(char* data, std::streamsize count) override {
        std::streamsize read = 0;
        while (read < count) {
            if (gptr() == egptr() && traits_type::eq_int_type(underflow(), traits_type::eof())) {
                break;
            }

            const auto available = static_cast<std::streamsize>(egptr() - gptr());
            const std::streamsize copy_size = std::min(count - read, available);
            std::memcpy(data + read, gptr(), static_cast<std::size_t>(copy_size));
            gbump(static_cast<int>(copy_size));
            read += copy_size;
        }
        return read;
    }

private:
    FileUtil::IOFile& file;
    ZSTD_DStream* stream{};
    std::array<u8, 64 * 1024> input_buffer{};
    std::array<char, 64 * 1024> output_buffer{};
    ZSTD_inBuffer input{input_buffer.data(), 0, 0};
    std::size_t compressed_remaining{};
    bool frame_finished{};
};

static std::string DecompressSaveStateData(std::span<const u8> compressed) {
    const std::size_t decompressed_size = Common::Compression::GetDecompressedSize(compressed);
    if (decompressed_size != ZSTD_CONTENTSIZE_UNKNOWN) {
        if (decompressed_size == ZSTD_CONTENTSIZE_ERROR || ZSTD_isError(decompressed_size)) {
            throw std::runtime_error("Invalid compressed savestate");
        }

        std::string decompressed(decompressed_size, '\0');
        const std::size_t result =
            ZSTD_decompress(decompressed.data(), decompressed.size(), compressed.data(),
                            compressed.size());
        if (ZSTD_isError(result)) {
            throw std::runtime_error(ZSTD_getErrorName(result));
        }
        if (result != decompressed_size) {
            throw std::runtime_error("Unexpected decompressed savestate size");
        }
        return decompressed;
    }

    ZSTD_DStream* stream = ZSTD_createDStream();
    if (!stream) {
        throw std::runtime_error("Failed to create ZSTD load state stream");
    }
    SCOPE_EXIT({ ZSTD_freeDStream(stream); });

    std::size_t result = ZSTD_initDStream(stream);
    if (ZSTD_isError(result)) {
        throw std::runtime_error(ZSTD_getErrorName(result));
    }

    std::string decompressed;
    std::array<char, 64 * 1024> output_buffer{};
    ZSTD_inBuffer input{compressed.data(), compressed.size(), 0};
    while (input.pos < input.size) {
        ZSTD_outBuffer output{output_buffer.data(), output_buffer.size(), 0};
        result = ZSTD_decompressStream(stream, &output, &input);
        if (ZSTD_isError(result)) {
            throw std::runtime_error(ZSTD_getErrorName(result));
        }
        if (output.pos != 0) {
            decompressed.append(output_buffer.data(), output.pos);
        }
    }

    if (result != 0) {
        throw std::runtime_error("Truncated compressed savestate");
    }
    return decompressed;
}
#endif

static std::string GetSaveStatePath(u64 program_id, u64 movie_id, u32 slot) {
    if (movie_id) {
        return fmt::format("{}{:016X}.movie{:016X}.{:02d}.cst",
                           FileUtil::GetUserPath(FileUtil::UserPath::StatesDir), program_id,
                           movie_id, slot);
    } else {
        return fmt::format("{}{:016X}.{:02d}.cst",
                           FileUtil::GetUserPath(FileUtil::UserPath::StatesDir), program_id, slot);
    }
}

static CSTHeader MakeSaveStateHeader(u64 program_id) {
    CSTHeader header{};
    header.filetype = header_magic_bytes;
    header.program_id = program_id;
    std::string rev_bytes;
    CryptoPP::StringSource ss(Common::g_scm_rev, true,
                              new CryptoPP::HexDecoder(new CryptoPP::StringSink(rev_bytes)));
    std::memcpy(header.revision.data(), rev_bytes.data(), sizeof(header.revision));
    header.time = std::chrono::duration_cast<std::chrono::seconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count();
    const std::string build_fullname = Common::g_build_fullname;
    std::memset(header.build_name.data(), 0, sizeof(header.build_name));
    std::memcpy(header.build_name.data(), build_fullname.c_str(),
                std::min(build_fullname.length(), sizeof(header.build_name) - 1));
    return header;
}

static bool ValidateSaveState(const CSTHeader& header, SaveStateInfo& info, u64 program_id,
                              u64 movie_id) {
    const auto path = GetSaveStatePath(program_id, movie_id, info.slot);
    if (header.filetype != header_magic_bytes) {
        LOG_WARNING(Core, "Invalid save state file {}", path);
        return false;
    }
    info.time = header.time;

    if (header.program_id != program_id) {
        LOG_WARNING(Core, "Save state file isn't for the current game {}", path);
        return false;
    }
    const std::string revision = fmt::format("{:02x}", fmt::join(header.revision, ""));
    const std::string build_name =
        header.zero == 0 ? reinterpret_cast<const char*>(header.build_name.data()) : "";

    if (revision == Common::g_scm_rev) {
        info.status = SaveStateInfo::ValidationStatus::OK;
    } else {
        if (!build_name.empty()) {
            info.build_name = build_name;
        } else if (hash_to_version.find(revision) != hash_to_version.end()) {
            info.build_name = hash_to_version.at(revision);
        }
        if (info.build_name.empty()) {
            LOG_WARNING(Core, "Save state file {} created from a different revision {}", path,
                        revision);
        } else {
            LOG_WARNING(Core,
                        "Save state file {} created from a different build {} with revision {}",
                        path, info.build_name, revision);
        }

        info.status = SaveStateInfo::ValidationStatus::RevisionDismatch;
    }
    return true;
}

std::vector<SaveStateInfo> ListSaveStates(u64 program_id, u64 movie_id) {
    std::vector<SaveStateInfo> result;
    result.reserve(SaveStateSlotCount);
    for (u32 slot = 0; slot <= SaveStateSlotCount; ++slot) {
        const auto path = GetSaveStatePath(program_id, movie_id, slot);
        if (!FileUtil::Exists(path)) {
            continue;
        }

        SaveStateInfo info;
        info.slot = slot;

        FileUtil::IOFile file(path, "rb");
        if (!file) {
            LOG_ERROR(Core, "Could not open file {}", path);
            continue;
        }
        CSTHeader header;
        if (file.GetSize() < sizeof(header)) {
            LOG_ERROR(Core, "File too small {}", path);
            continue;
        }
        if (file.ReadBytes(&header, sizeof(header)) != sizeof(header)) {
            LOG_ERROR(Core, "Could not read from file {}", path);
            continue;
        }
        if (!ValidateSaveState(header, info, program_id, movie_id)) {
            continue;
        }

        result.emplace_back(std::move(info));
    }
    return result;
}

void System::SaveState(u32 slot) const {
    if (app_loader) {
        if (!app_loader->SupportsSaveStates()) {
            throw std::runtime_error("The current app loader doesn't support save states");
        }
    }

#ifdef __SWITCH__
    const u64 movie_id = movie.GetCurrentMovieID();
    const auto path = GetSaveStatePath(title_id, movie_id, slot);
    const auto temp_path = path + ".tmp";
    const auto backup_path = path + ".bak";
    if (!FileUtil::CreateFullPath(path)) {
        throw std::runtime_error("Could not create path " + path);
    }

    bool committed = false;
    bool backup_created = false;
    SCOPE_EXIT({
        if (!committed) {
            FileUtil::Delete(temp_path);
            if (backup_created && !FileUtil::Exists(path)) {
                FileUtil::Rename(backup_path, path);
            }
        }
    });

    FileUtil::IOFile file(temp_path, "wb");
    if (!file) {
        throw std::runtime_error("Could not open file " + temp_path);
    }

    const CSTHeader header = MakeSaveStateHeader(title_id);
    if (file.WriteBytes(&header, sizeof(header)) != sizeof(header)) {
        throw std::runtime_error("Could not write to file " + temp_path);
    }

    ZstdSaveStateOutputStreamBuf compressed_stream{file};
    std::ostream stream{&compressed_stream};
    {
        oarchive oa{stream};
        oa&* this;
    }
    stream.flush();
    if (!stream) {
        throw std::runtime_error("Could not write to file " + temp_path);
    }

    if (!compressed_stream.Finish()) {
        throw std::runtime_error("Could not write to file " + temp_path);
    }

    LOG_INFO(Core, "Serialized save state size: {} bytes", compressed_stream.GetInputBytes());

    if (!file.Close()) {
        throw std::runtime_error("Could not close file " + temp_path);
    }

    FileUtil::Delete(backup_path);
    if (FileUtil::Exists(path)) {
        if (!FileUtil::Rename(path, backup_path)) {
            throw std::runtime_error("Could not backup save state " + path);
        }
        backup_created = true;
    }
    if (!FileUtil::Rename(temp_path, path)) {
        const bool restored = backup_created && FileUtil::Rename(backup_path, path);
        backup_created = !restored;
        throw std::runtime_error(restored ? "Could not replace save state " + path
                                          : "Could not replace save state " + path +
                                                " (old state kept at " + backup_path + ")");
    }
    committed = true;
    if (backup_created) {
        FileUtil::Delete(backup_path);
    }
#else
    std::ostringstream sstream{std::ios_base::binary};
    // Serialize
    oarchive oa{sstream};
    oa&* this;

    std::string str{std::move(sstream).str()};
    const auto data = std::span<const u8>{reinterpret_cast<const u8*>(str.data()), str.size()};
    LOG_INFO(Core, "Serialized save state size: {} bytes", data.size());
    auto buffer = CompressSaveStateData(data);

    const u64 movie_id = movie.GetCurrentMovieID();
    const auto path = GetSaveStatePath(title_id, movie_id, slot);
    if (!FileUtil::CreateFullPath(path)) {
        throw std::runtime_error("Could not create path " + path);
    }

    FileUtil::IOFile file(path, "wb");
    if (!file) {
        throw std::runtime_error("Could not open file " + path);
    }

    const CSTHeader header = MakeSaveStateHeader(title_id);

    if (file.WriteBytes(&header, sizeof(header)) != sizeof(header) ||
        file.WriteBytes(buffer.data(), buffer.size()) != buffer.size()) {
        throw std::runtime_error("Could not write to file " + path);
    }
#endif
}

void System::LoadState(u32 slot) {
    if (app_loader) {
        if (!app_loader->SupportsSaveStates()) {
            throw std::runtime_error("The current app loader doesn't support save states");
        }
    }
    const auto room_member = Network::GetRoomMember().lock();
    if (room_member && room_member->IsConnected()) {
        throw std::runtime_error("Unable to load while connected to multiplayer");
    }

    const u64 movie_id = movie.GetCurrentMovieID();
    const auto path = GetSaveStatePath(title_id, movie_id, slot);

#ifdef __SWITCH__
    const u64 file_size = FileUtil::GetSize(path);
    if (file_size < sizeof(CSTHeader)) {
        throw std::runtime_error("Save state too small");
    }

    FileUtil::IOFile file(path, "rb");
    if (!file) {
        throw std::runtime_error("Could not open file " + path);
    }

    CSTHeader header;
    if (file.ReadBytes(&header, sizeof(header)) != sizeof(header)) {
        throw std::runtime_error("Could not read from file at " + path);
    }

    SaveStateInfo info;
    info.slot = slot;
    if (!ValidateSaveState(header, info, title_id, movie_id)) {
        throw std::runtime_error("Invalid savestate");
    }

    ZstdSaveStateInputStreamBuf decompressed_stream{
        file, static_cast<std::size_t>(file_size - sizeof(CSTHeader))};
    iarchive ia{decompressed_stream};
    ia&* this;
#else
    std::vector<u8> decompressed;
    {
        std::vector<u8> buffer(FileUtil::GetSize(path) - sizeof(CSTHeader));

        FileUtil::IOFile file(path, "rb");

        // load header
        CSTHeader header;
        if (file.ReadBytes(&header, sizeof(header)) != sizeof(header)) {
            throw std::runtime_error("Could not read from file at " + path);
        }

        // validate header
        SaveStateInfo info;
        info.slot = slot;
        if (!ValidateSaveState(header, info, title_id, movie_id)) {
            throw std::runtime_error("Invalid savestate");
        }

        if (file.ReadBytes(buffer.data(), buffer.size()) != buffer.size()) {
            throw std::runtime_error("Could not read from file at " + path);
        }
        decompressed = Common::Compression::DecompressDataZSTD(buffer);
    }
    std::istringstream sstream{
        std::string{reinterpret_cast<char*>(decompressed.data()), decompressed.size()},
        std::ios_base::binary};
    decompressed.clear();

    // Deserialize
    iarchive ia{sstream};
    ia&* this;
#endif
}

std::vector<u8> System::SaveStateBuffer() const {
    std::ostringstream sstream{std::ios_base::binary};
    // Serialize
    oarchive oa{sstream};
    oa&* this;

    std::string str{std::move(sstream).str()};
    const auto data = std::span<const u8>{reinterpret_cast<const u8*>(str.data()), str.size()};
    auto buffer = CompressSaveStateData(data);

    const CSTHeader header = MakeSaveStateHeader(title_id);

    std::vector<u8> result(reinterpret_cast<const u8*>(&header),
                           reinterpret_cast<const u8*>(&header) + sizeof(header));
    std::copy(buffer.begin(), buffer.end(), std::back_inserter(result));

    return result;
}

bool System::LoadStateBuffer(std::vector<u8> buffer) {
    CSTHeader header;

    if (buffer.size() < sizeof(header)) {
        LOG_ERROR(Core, "Save state too small");
        return false;
    }

    header = *((CSTHeader*)buffer.data());

    if (header.filetype != header_magic_bytes) {
        LOG_ERROR(Core, "Invalid save state");
        return false;
    }

    if (header.program_id != title_id) {
        LOG_ERROR(Core, "Save state isn't for the current game");
        return false;
    }
    std::string revision = fmt::format("{:02x}", fmt::join(header.revision, ""));
    if (revision != Common::g_scm_rev) {
        LOG_ERROR(Core,
                  "Save state file created from a different revision (core: {}, savestate: {})",
                  Common::g_scm_rev, revision);
        return false;
    }

    std::vector<u8> state(buffer.begin() + sizeof(CSTHeader), buffer.end());
#ifdef __SWITCH__
    auto decompressed = DecompressSaveStateData(state);
    std::istringstream sstream{std::move(decompressed), std::ios_base::binary};
#else
    auto decompressed = Common::Compression::DecompressDataZSTD(state);

    std::istringstream sstream{
        std::string{reinterpret_cast<char*>(decompressed.data()), decompressed.size()},
        std::ios_base::binary};
    decompressed.clear();
#endif

    // Deserialize
    iarchive ia{sstream};
    ia&* this;

    return true;
}

} // namespace Core
