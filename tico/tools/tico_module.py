#!/usr/bin/env python3
"""Settings definition and translations for the tico module of Azahar.

tico/module/settings.json lists every setting tico's settings screen and the
quick menu show, laid out in tabs. Labels are translation keys; the strings go
into tico/lang/*.json (settings_azahar_*), next to the overlay's own strings.

The keys are what src/tico/tico_settings.cpp reads (azahar_*); add an option
there and here together.

    python3 tico/tools/tico_module.py
"""

from __future__ import annotations

import json
import re
from collections import OrderedDict
from pathlib import Path

TICO = Path(__file__).resolve().parents[1]
SETTINGS = TICO / "module/settings.json"
LANG_DIR = TICO / "lang"
LANGUAGES = ("en", "de", "es", "fr", "ja", "pt", "ru", "zh")
P = "settings_azahar_"

# label key -> en, de, es, fr, ja, pt, ru, zh
STRINGS = {
    # tabs and sections
    "system": ("System", "System", "Sistema", "Système", "システム", "Sistema", "Система", "系统"),
    "console": ("Console", "Konsole", "Consola", "Console", "本体", "Console", "Консоль", "主机"),
    "cpu": ("CPU", "CPU", "CPU", "CPU", "CPU", "CPU", "ЦП", "CPU"),
    "graphics": ("Graphics", "Grafik", "Gráficos", "Graphismes", "グラフィック", "Gráficos", "Графика", "图形"),
    "renderer": ("Renderer", "Renderer", "Renderizador", "Rendu", "レンダラー", "Renderizador", "Рендерер", "渲染器"),
    "textures": ("Textures", "Texturen", "Texturas", "Textures", "テクスチャ", "Texturas", "Текстуры", "纹理"),
    "screen": ("Screen", "Bildschirm", "Pantalla", "Écran", "画面", "Tela", "Экран", "屏幕"),
    "layout": ("Layout", "Anordnung", "Disposición", "Disposition", "レイアウト", "Layout", "Раскладка", "布局"),
    "audio": ("Audio", "Audio", "Audio", "Audio", "オーディオ", "Áudio", "Звук", "音频"),
    "output": ("Output", "Ausgabe", "Salida", "Sortie", "出力", "Saída", "Вывод", "输出"),
    "microphone": ("Microphone", "Mikrofon", "Micrófono", "Microphone", "マイク", "Microfone", "Микрофон", "麦克风"),
    # options
    "new_3ds": ("New 3DS", "New 3DS", "New 3DS", "New 3DS", "New 3DS", "New 3DS", "New 3DS", "New 3DS"),
    "region": ("System Region", "Systemregion", "Región del sistema", "Région du système", "本体の地域", "Região do sistema", "Регион системы", "系统区域"),
    "language": ("System Language", "Systemsprache", "Idioma del sistema", "Langue du système", "本体の言語", "Idioma do sistema", "Язык системы", "系统语言"),
    "username": ("Username", "Benutzername", "Nombre de usuario", "Nom d'utilisateur", "ユーザー名", "Nome de usuário", "Имя пользователя", "用户名"),
    "use_virtual_sd": ("Virtual SD Card", "Virtuelle SD-Karte", "Tarjeta SD virtual", "Carte SD virtuelle", "仮想SDカード", "Cartão SD virtual", "Виртуальная SD-карта", "虚拟SD卡"),
    "boost_mode": ("Boost mode (CPU 1785 MHz, GPU 768 MHz)", "Boost-Modus (CPU 1785 MHz, GPU 768 MHz)",
                   "Modo boost (CPU 1785 MHz, GPU 768 MHz)", "Mode boost (CPU 1785 MHz, GPU 768 MHz)",
                   "ブーストモード（CPU 1785 MHz、GPU 768 MHz）", "Modo boost (CPU 1785 MHz, GPU 768 MHz)",
                   "Режим ускорения (CPU 1785 МГц, GPU 768 МГц)", "加速模式（CPU 1785 MHz，GPU 768 MHz）"),
    "async_gpu": ("GPU on its own thread", "GPU in eigenem Thread", "GPU en su propio hilo",
                  "GPU sur son propre thread", "GPUを別スレッドで実行", "GPU em thread própria",
                  "GPU в отдельном потоке", "GPU 独立线程"),
    "fastmem": ("Fastmem", "Fastmem", "Fastmem", "Fastmem", "Fastmem", "Fastmem", "Fastmem", "Fastmem"),
    "cpu_clock": ("CPU Clock", "CPU-Takt", "Reloj de CPU", "Fréquence CPU", "CPUクロック", "Clock da CPU", "Частота ЦП", "CPU 频率"),
    "resolution": ("Internal Resolution", "Interne Auflösung", "Resolución interna", "Résolution interne", "内部解像度", "Resolução interna", "Внутреннее разрешение", "内部分辨率"),
    "use_hw_shader": ("Hardware Shaders", "Hardware-Shader", "Shaders por hardware", "Shaders matériels", "ハードウェアシェーダー", "Shaders por hardware", "Аппаратные шейдеры", "硬件着色器"),
    "shader_jit": ("Shader JIT", "Shader-JIT", "JIT de shaders", "JIT des shaders", "シェーダーJIT", "JIT de shaders", "JIT шейдеров", "着色器 JIT"),
    "accurate_mul": ("Accurate Multiplication", "Genaue Multiplikation", "Multiplicación precisa", "Multiplication précise", "正確な乗算", "Multiplicação precisa", "Точное умножение", "精确乘法"),
    "disk_shader_cache": ("Shader Cache", "Shader-Cache", "Caché de shaders", "Cache des shaders", "シェーダーキャッシュ", "Cache de shaders", "Кэш шейдеров", "着色器缓存"),
    "async_shaders": ("Async Shaders", "Asynchrone Shader", "Shaders asíncronos", "Shaders asynchrones", "非同期シェーダー", "Shaders assíncronos", "Асинхронные шейдеры", "异步着色器"),
    "shader_notice": ("Show Shader Compiling", "Shader-Kompilierung anzeigen", "Mostrar compilación de shaders",
                      "Afficher la compilation des shaders", "シェーダーのコンパイルを表示", "Mostrar compilação de shaders",
                      "Показывать компиляцию шейдеров", "显示着色器编译"),
    # the notice itself, drawn during play
    "compiling_shaders": ("Compiling shaders", "Shader werden kompiliert", "Compilando shaders",
                          "Compilation des shaders", "シェーダーをコンパイル中", "Compilando shaders",
                          "Компиляция шейдеров", "正在编译着色器"),
    "hacks": ("Speed Hacks", "Geschwindigkeits-Hacks", "Trucos de velocidad", "Astuces de vitesse",
              "高速化ハック", "Truques de velocidade", "Ускоряющие хаки", "加速选项"),
    "skip_slow_draw": ("Skip Slow Draws", "Langsame Zeichnungen überspringen", "Omitir dibujos lentos",
                       "Ignorer les rendus lents", "遅い描画をスキップ", "Pular desenhos lentos",
                       "Пропускать медленную отрисовку", "跳过慢速绘制"),
    "skip_texture_copy": ("Skip Uncached Texture Copies", "Ungecachte Texturkopien überspringen",
                          "Omitir copias de texturas sin caché", "Ignorer les copies de textures non cachées",
                          "未キャッシュのテクスチャコピーをスキップ", "Pular cópias de texturas sem cache",
                          "Пропускать некэшированные копии текстур", "跳过未缓存的纹理复制"),
    "skip_cpu_write": ("Skip Small CPU Writes", "Kleine CPU-Schreibzugriffe überspringen",
                       "Omitir escrituras pequeñas de la CPU", "Ignorer les petites écritures CPU",
                       "小さなCPU書き込みをスキップ", "Pular pequenas escritas da CPU",
                       "Пропускать мелкие записи CPU", "跳过小型CPU写入"),
    "vsync": ("VSync", "VSync", "VSync", "VSync", "垂直同期", "VSync", "Вертикальная синхронизация", "垂直同步"),
    "simulate_gpu_timings": ("Simulate 3DS GPU Timings", "3DS-GPU-Timings simulieren", "Simular tiempos de GPU de 3DS", "Simuler les délais du GPU 3DS", "3DSのGPUタイミングを再現", "Simular tempos da GPU do 3DS", "Имитировать тайминги GPU 3DS", "模拟 3DS GPU 时序"),
    "right_eye": ("Right Eye Rendering", "Rechtes Auge rendern", "Renderizar ojo derecho", "Rendu de l'œil droit", "右目の描画", "Renderizar olho direito", "Рендер правого глаза", "渲染右眼"),
    "texture_filter": ("Texture Filter", "Texturfilter", "Filtro de texturas", "Filtre de textures", "テクスチャフィルター", "Filtro de texturas", "Фильтр текстур", "纹理过滤"),
    "texture_sampling": ("Texture Sampling", "Textur-Sampling", "Muestreo de texturas", "Échantillonnage des textures", "テクスチャサンプリング", "Amostragem de texturas", "Выборка текстур", "纹理采样"),
    "anisotropic_filtering": ("Anisotropic Filtering", "Anisotrope Filterung", "Filtrado anisotrópico",
                              "Filtrage anisotrope", "異方性フィルタリング", "Filtragem anisotrópica",
                              "Анизотропная фильтрация", "各向异性过滤"),
    "custom_textures": ("Custom Textures", "Eigene Texturen", "Texturas personalizadas", "Textures personnalisées", "カスタムテクスチャ", "Texturas personalizadas", "Свои текстуры", "自定义纹理"),
    "dump_textures": ("Dump Textures", "Texturen ausgeben", "Volcar texturas", "Extraire les textures", "テクスチャをダンプ", "Extrair texturas", "Сохранять текстуры", "导出纹理"),
    "layout_option": ("Screen Layout", "Bildschirmanordnung", "Disposición de pantallas", "Disposition des écrans", "画面レイアウト", "Layout das telas", "Раскладка экранов", "屏幕布局"),
    "orientation": ("Orientation", "Ausrichtung", "Orientación", "Orientation", "向き", "Orientação", "Ориентация", "方向"),
    "large_screen_proportion": ("Large Screen Proportion", "Größe des großen Bildschirms", "Proporción de pantalla grande", "Proportion du grand écran", "大画面の比率", "Proporção da tela grande", "Пропорция большого экрана", "大屏比例"),
    "display_size": ("Display Size", "Anzeigegröße", "Tamaño de pantalla", "Taille d'affichage", "表示サイズ", "Tamanho da exibição", "Размер изображения", "显示大小"),
    "swap_screens": ("Swap Screens", "Bildschirme tauschen", "Intercambiar pantallas", "Inverser les écrans", "画面を入れ替え", "Trocar telas", "Поменять экраны", "交换屏幕"),
    "volume": ("Volume", "Lautstärke", "Volumen", "Volume", "音量", "Volume", "Громкость", "音量"),
    "audio_stretching": ("Audio Stretching", "Audio-Stretching", "Estiramiento de audio", "Étirement audio", "オーディオストレッチ", "Esticamento de áudio", "Растяжение звука", "音频拉伸"),
    "controls": ("Controls", "Steuerung", "Controles", "Commandes", "操作", "Controles", "Управление", "控制"),
    "button_mapping": ("Button Mapping", "Tastenbelegung", "Asignación de botones", "Attribution des boutons", "ボタン割り当て", "Mapeamento de botões", "Назначение кнопок", "按键映射"),
    "sticks": ("Sticks", "Sticks", "Palancas", "Sticks", "スティック", "Analógicos", "Стики", "摇杆"),
    "map_a": ("A Button", "A-Taste", "Botón A", "Bouton A", "Aボタン", "Botão A", "Кнопка A", "A 键"),
    "map_b": ("B Button", "B-Taste", "Botón B", "Bouton B", "Bボタン", "Botão B", "Кнопка B", "B 键"),
    "map_x": ("X Button", "X-Taste", "Botón X", "Bouton X", "Xボタン", "Botão X", "Кнопка X", "X 键"),
    "map_y": ("Y Button", "Y-Taste", "Botón Y", "Bouton Y", "Yボタン", "Botão Y", "Кнопка Y", "Y 键"),
    "map_l": ("L Button", "L-Taste", "Botón L", "Bouton L", "Lボタン", "Botão L", "Кнопка L", "L 键"),
    "map_r": ("R Button", "R-Taste", "Botón R", "Bouton R", "Rボタン", "Botão R", "Кнопка R", "R 键"),
    "map_zl": ("ZL Button", "ZL-Taste", "Botón ZL", "Bouton ZL", "ZLボタン", "Botão ZL", "Кнопка ZL", "ZL 键"),
    "map_zr": ("ZR Button", "ZR-Taste", "Botón ZR", "Bouton ZR", "ZRボタン", "Botão ZR", "Кнопка ZR", "ZR 键"),
    "map_start": ("START", "START", "START", "START", "START", "START", "START", "START"),
    "map_select": ("SELECT", "SELECT", "SELECT", "SELECT", "SELECT", "SELECT", "SELECT", "SELECT"),
    "map_up": ("D-Pad Up", "Steuerkreuz oben", "Cruceta arriba", "Croix haut", "十字ボタン上", "Direcional para cima", "Крестовина вверх", "十字键上"),
    "map_down": ("D-Pad Down", "Steuerkreuz unten", "Cruceta abajo", "Croix bas", "十字ボタン下", "Direcional para baixo", "Крестовина вниз", "十字键下"),
    "map_left": ("D-Pad Left", "Steuerkreuz links", "Cruceta izquierda", "Croix gauche", "十字ボタン左", "Direcional para a esquerda", "Крестовина влево", "十字键左"),
    "map_right": ("D-Pad Right", "Steuerkreuz rechts", "Cruceta derecha", "Croix droite", "十字ボタン右", "Direcional para a direita", "Крестовина вправо", "十字键右"),
    "map_home": ("HOME Button", "HOME-Taste", "Botón HOME", "Bouton HOME", "HOMEボタン", "Botão HOME", "Кнопка HOME", "HOME 键"),
    "map_circle_pad": ("Circle Pad", "Schiebepad", "Botón deslizante", "Pad circulaire", "スライドパッド", "Circle Pad", "Circle Pad", "滑杆"),
    "map_c_stick": ("C-Stick", "C-Stick", "Palanca C", "Stick C", "Cスティック", "C-Stick", "C-стик", "C 摇杆"),
    "mic_input": ("Microphone Input", "Mikrofoneingang", "Entrada de micrófono", "Entrée micro", "マイク入力", "Entrada do microfone", "Вход микрофона", "麦克风输入"),
}

# choice label -> de, es, fr, ja, pt, ru, zh (settings_azahar_value_<slug>);
# labels missing here stay as written (numbers, names)
VALUES = {
    "Auto": ("Auto", "Auto", "Auto", "自動", "Auto", "Авто", "自动"),
    "Japan": ("Japan", "Japón", "Japon", "日本", "Japão", "Япония", "日本"),
    "USA": ("USA", "EE. UU.", "États-Unis", "アメリカ", "EUA", "США", "美国"),
    "Europe": ("Europa", "Europa", "Europe", "ヨーロッパ", "Europa", "Европа", "欧洲"),
    "Australia": ("Australien", "Australia", "Australie", "オーストラリア", "Austrália", "Австралия", "澳大利亚"),
    "China": ("China", "China", "Chine", "中国", "China", "Китай", "中国"),
    "Korea": ("Korea", "Corea", "Corée", "韓国", "Coreia", "Корея", "韩国"),
    "Taiwan": ("Taiwan", "Taiwán", "Taïwan", "台湾", "Taiwan", "Тайвань", "台湾"),
    "Japanese": ("Japanisch", "Japonés", "Japonais", "日本語", "Japonês", "Японский", "日语"),
    "English": ("Englisch", "Inglés", "Anglais", "英語", "Inglês", "Английский", "英语"),
    "French": ("Französisch", "Francés", "Français", "フランス語", "Francês", "Французский", "法语"),
    "German": ("Deutsch", "Alemán", "Allemand", "ドイツ語", "Alemão", "Немецкий", "德语"),
    "Italian": ("Italienisch", "Italiano", "Italien", "イタリア語", "Italiano", "Итальянский", "意大利语"),
    "Spanish": ("Spanisch", "Español", "Espagnol", "スペイン語", "Espanhol", "Испанский", "西班牙语"),
    "Simplified Chinese": ("Vereinfachtes Chinesisch", "Chino simplificado", "Chinois simplifié", "簡体字中国語", "Chinês simplificado", "Упрощённый китайский", "简体中文"),
    "Korean": ("Koreanisch", "Coreano", "Coréen", "韓国語", "Coreano", "Корейский", "韩语"),
    "Dutch": ("Niederländisch", "Neerlandés", "Néerlandais", "オランダ語", "Holandês", "Нидерландский", "荷兰语"),
    "Portuguese": ("Portugiesisch", "Portugués", "Portugais", "ポルトガル語", "Português", "Португальский", "葡萄牙语"),
    "Russian": ("Russisch", "Ruso", "Russe", "ロシア語", "Russo", "Русский", "俄语"),
    "Traditional Chinese": ("Traditionelles Chinesisch", "Chino tradicional", "Chinois traditionnel", "繁体字中国語", "Chinês tradicional", "Традиционный китайский", "繁体中文"),
    "Native": ("Nativ", "Nativa", "Native", "ネイティブ", "Nativa", "Родное", "原生"),
    "None": ("Keiner", "Ninguno", "Aucun", "なし", "Nenhum", "Нет", "无"),
    "Game Controlled": ("Vom Spiel gesteuert", "Controlado por el juego", "Contrôlé par le jeu", "ゲームに従う", "Controlado pelo jogo", "Как в игре", "由游戏控制"),
    "Nearest Neighbor": ("Nächster Nachbar", "Vecino más cercano", "Plus proche voisin", "ニアレストネイバー", "Vizinho mais próximo", "Ближайший сосед", "最近邻"),
    "Linear": ("Linear", "Lineal", "Linéaire", "リニア", "Linear", "Линейная", "线性"),
    "Default": ("Standard", "Predeterminada", "Par défaut", "標準", "Padrão", "Стандартная", "默认"),
    "Single Screen": ("Ein Bildschirm", "Una pantalla", "Écran unique", "1画面", "Tela única", "Один экран", "单屏"),
    "Large Screen": ("Großer Bildschirm", "Pantalla grande", "Grand écran", "大画面", "Tela grande", "Большой экран", "大屏"),
    "Large Screen Inverted": ("Großer Bildschirm, gespiegelt", "Pantalla grande invertida", "Grand écran inversé", "大画面（反転）", "Tela grande invertida", "Большой экран, зеркально", "大屏（反转）"),
    "Side by Side": ("Nebeneinander", "Lado a lado", "Côte à côte", "横並び", "Lado a lado", "Рядом", "并排"),
    "Hybrid": ("Hybrid", "Híbrida", "Hybride", "ハイブリッド", "Híbrido", "Гибридная", "混合"),
    "Hybrid Inverted": ("Hybrid, gespiegelt", "Híbrida invertida", "Hybride inversé", "ハイブリッド（反転）", "Híbrido invertido", "Гибридная, зеркально", "混合（反转）"),
    "Horizontal": ("Horizontal", "Horizontal", "Horizontale", "横", "Horizontal", "Горизонтальная", "横向"),
    "Vertical": ("Vertikal", "Vertical", "Verticale", "縦", "Vertical", "Вертикальная", "纵向"),
    "Horizontal Inverted": ("Horizontal, gedreht", "Horizontal invertida", "Horizontale inversée", "横（反転）", "Horizontal invertida", "Горизонтальная, перевёрнутая", "横向（反转）"),
    "Vertical Inverted": ("Vertikal, gedreht", "Vertical invertida", "Verticale inversée", "縦（反転）", "Vertical invertida", "Вертикальная, перевёрнутая", "纵向（反转）"),
    "Fill": ("Füllen", "Llenar", "Remplir", "フィット", "Preencher", "Заполнить", "填充"),
    "Stretch": ("Strecken", "Estirar", "Étirer", "引き伸ばし", "Esticar", "Растянуть", "拉伸"),
    "Original": ("Original", "Original", "Original", "オリジナル", "Original", "Исходный", "原始"),
    "Plus": ("Plus", "Más", "Plus", "プラス", "Mais", "Плюс", "加号"),
    "Minus": ("Minus", "Menos", "Moins", "マイナス", "Menos", "Минус", "减号"),
    "Left stick": ("Linker Stick", "Palanca izquierda", "Stick gauche", "左スティック", "Analógico esquerdo", "Левый стик", "左摇杆"),
    "Right stick": ("Rechter Stick", "Palanca derecha", "Stick droit", "右スティック", "Analógico direito", "Правый стик", "右摇杆"),
    "Left stick press": ("Linker Stick drücken", "Pulsar palanca izquierda", "Clic stick gauche", "左スティック押し込み", "Pressionar analógico esquerdo", "Нажатие левого стика", "按下左摇杆"),
    "Right stick press": ("Rechter Stick drücken", "Pulsar palanca derecha", "Clic stick droit", "右スティック押し込み", "Pressionar analógico direito", "Нажатие правого стика", "按下右摇杆"),
    "Up": ("Oben", "Arriba", "Haut", "上", "Cima", "Вверх", "上"),
    "Down": ("Unten", "Abajo", "Bas", "下", "Baixo", "Вниз", "下"),
    "Left": ("Links", "Izquierda", "Gauche", "左", "Esquerda", "Влево", "左"),
    "Right": ("Rechts", "Derecha", "Droite", "右", "Direita", "Вправо", "右"),
    "Off": ("Aus", "No", "Non", "オフ", "Desligado", "Выкл.", "关"),
    "Disabled": ("Aus", "Desactivado", "Désactivé", "なし", "Desativado", "Отключено", "禁用"),
    "Static Noise": ("Rauschen", "Ruido estático", "Bruit statique", "ノイズ", "Ruído estático", "Шум", "静态噪声"),
}


def choices(*pairs):
    return [{"label": label, "value": value} for label, value in pairs]


def same(*labels):
    return choices(*((label, label) for label in labels))


def option(key, label, kind, default, restart=False, **extra):
    entry = OrderedDict(key="azahar_" + key, label=P + label, type=kind, default=default)
    if restart:
        entry["restart"] = True
    entry.update(extra)
    return entry


REGIONS = same("Auto", "Japan", "USA", "Europe", "Australia", "China", "Korea", "Taiwan")
LANGUAGES_3DS = same("Japanese", "English", "French", "German", "Italian", "Spanish",
                     "Simplified Chinese", "Korean", "Dutch", "Portuguese", "Russian",
                     "Traditional Chinese")
CPU_CLOCKS = choices(*((f"{p}%", str(p)) for p in (25, 50, 75, 100, 125, 150, 175, 200, 250, 300,
                                                    350, 400)))
RESOLUTIONS = choices(("Native", "1"), *((f"{n}x", str(n)) for n in range(2, 11)))
PROPORTIONS = choices(*((f"{v:.2f}", f"{v:.2f}") for v in [1.0 + 0.5 * i for i in range(11)]))
VOLUMES = choices(*((f"{v}%", str(v)) for v in range(0, 101, 10)))

# the Switch buttons a 3DS button can be on (src/tico/tico_settings.cpp knows them)
SWITCH_BUTTONS = choices(("A", "A"), ("B", "B"), ("X", "X"), ("Y", "Y"), ("L", "L"), ("R", "R"),
                         ("ZL", "ZL"), ("ZR", "ZR"), ("Plus", "Plus"), ("Minus", "Minus"),
                         ("Left stick press", "StickL"), ("Right stick press", "StickR"),
                         ("Up", "Up"), ("Down", "Down"), ("Left", "Left"), ("Right", "Right"),
                         ("Disabled", "None"))
STICKS = choices(("Left stick", "Left"), ("Right stick", "Right"), ("Disabled", "None"))


def button(name, default):
    return option("map_" + name, "map_" + name, "enum", default, choices=SWITCH_BUTTONS)


TABS = [
    ("system", [
        ("console", [
            option("new_3ds", "new_3ds", "bool", "true", restart=True),
            option("region", "region", "enum", "Auto", restart=True, choices=REGIONS),
            option("language", "language", "enum", "English", restart=True, choices=LANGUAGES_3DS),
            option("username", "username", "string", "AZAHAR", restart=True, max_length=10),
            option("use_virtual_sd", "use_virtual_sd", "bool", "true", restart=True),
        ]),
        ("cpu", [
            option("cpu_clock", "cpu_clock", "enum", "100", choices=CPU_CLOCKS),
            option("boost_mode", "boost_mode", "bool", "true", restart=True),
            option("fastmem", "fastmem", "bool", "true", restart=True),
            option("async_gpu", "async_gpu", "bool", "true", restart=True),
        ]),
    ]),
    ("graphics", [
        ("renderer", [
            option("resolution", "resolution", "enum", "1", choices=RESOLUTIONS),
            option("use_hw_shader", "use_hw_shader", "bool", "true", restart=True),
            option("shader_jit", "shader_jit", "bool", "true", restart=True),
            option("accurate_mul", "accurate_mul", "bool", "true", restart=True),
            option("disk_shader_cache", "disk_shader_cache", "bool", "true", restart=True),
            option("async_shaders", "async_shaders", "bool", "true", restart=True),
            option("shader_notice", "shader_notice", "bool", "false"),
            option("vsync", "vsync", "bool", "true"),
            option("simulate_gpu_timings", "simulate_gpu_timings", "bool", "false"),
            option("right_eye", "right_eye", "bool", "false"),
        ]),
        ("textures", [
            option("texture_filter", "texture_filter", "enum", "none",
                   choices=choices(("None", "none"), ("Anime4K Ultrafast", "Anime4K Ultrafast"),
                                   ("Bicubic", "Bicubic"), ("ScaleForce", "ScaleForce"),
                                   ("xBRZ", "xBRZ"), ("MMPX", "MMPX"))),
            option("texture_sampling", "texture_sampling", "enum", "GameControlled",
                   choices=choices(("Game Controlled", "GameControlled"),
                                   ("Nearest Neighbor", "NearestNeighbor"), ("Linear", "Linear"))),
            option("anisotropic_filtering", "anisotropic_filtering", "enum", "16x",
                   choices=choices(("Off", "1x"), ("2x", "2x"), ("4x", "4x"), ("8x", "8x"),
                                   ("16x", "16x"))),
            option("custom_textures", "custom_textures", "bool", "false", restart=True),
            option("dump_textures", "dump_textures", "bool", "false", restart=True),
        ]),
        # speed over accuracy; all off by default
        ("hacks", [
            option("skip_slow_draw", "skip_slow_draw", "bool", "false"),
            option("skip_texture_copy", "skip_texture_copy", "bool", "false"),
            option("skip_cpu_write", "skip_cpu_write", "bool", "false"),
        ]),
    ]),
    ("screen", [
        ("layout", [
            option("layout", "layout_option", "enum", "default",
                   choices=choices(("Default", "default"), ("Single Screen", "single"),
                                   ("Large Screen", "large"),
                                   ("Large Screen Inverted", "large_inverted"),
                                   ("Side by Side", "side"), ("Hybrid", "hybrid"),
                                   ("Hybrid Inverted", "hybrid_inverted"))),
            option("large_screen_proportion", "large_screen_proportion", "enum", "4.00",
                   choices=PROPORTIONS),
            option("orientation", "orientation", "enum", "horizontal",
                   choices=choices(("Horizontal", "horizontal"), ("Vertical", "vertical"),
                                   ("Horizontal Inverted", "horizontal_inverted"),
                                   ("Vertical Inverted", "vertical_inverted"))),
            option("display_size", "display_size", "enum", "Fill",
                   choices=same("Fill", "Stretch", "Original")),
            option("swap_screens", "swap_screens", "bool", "false"),
        ]),
    ]),
    ("controls", [
        ("button_mapping", [
            button("a", "A"), button("b", "B"), button("x", "X"), button("y", "Y"),
            button("l", "L"), button("r", "R"), button("zl", "ZL"), button("zr", "ZR"),
            button("start", "Plus"), button("select", "Minus"),
            button("up", "Up"), button("down", "Down"), button("left", "Left"),
            button("right", "Right"), button("home", "None"),
        ]),
        ("sticks", [
            option("map_circle_pad", "map_circle_pad", "enum", "Left", choices=STICKS),
            option("map_c_stick", "map_c_stick", "enum", "Right", choices=STICKS),
        ]),
    ]),
    ("audio", [
        ("output", [
            option("volume", "volume", "enum", "100", choices=VOLUMES),
            option("audio_stretching", "audio_stretching", "bool", "true"),
        ]),
        ("microphone", [
            option("mic_input", "mic_input", "enum", "none", restart=True,
                   choices=choices(("None", "none"), ("Auto", "auto"),
                                   ("Static Noise", "static_noise"))),
        ]),
    ]),
]


def value_key(label: str) -> str:
    """settings_azahar_value_ and the label in lower case, as tico_config.cpp builds it."""
    return P + "value_" + re.sub(r"[^a-z0-9]+", "_", label.lower()).strip("_")


def build_settings() -> OrderedDict:
    settings = OrderedDict(
        core_id="azahar",
        display_name="Azahar",
        config_file="azahar.jsonc",
        slugs=["3ds", "citra", "azahar"],
        is_libretro=False,
        bool_true_value="true",
        bool_false_value="false",
        tabs=[],
    )
    for tab, sections in TABS:
        settings["tabs"].append(OrderedDict(
            name=P + tab,
            sections=[OrderedDict(title=P + title, options=options)
                      for title, options in sections]))
    return settings


def build_strings(settings) -> dict[str, dict[str, str]]:
    strings = {lang: {} for lang in LANGUAGES}
    for key, texts in STRINGS.items():
        for lang, text in zip(LANGUAGES, texts):
            strings[lang][P + key] = text
    used = {choice["label"] for tab in settings["tabs"] for section in tab["sections"]
            for entry in section["options"] for choice in entry.get("choices", [])}
    for label in sorted(used & VALUES.keys()):
        texts = (label,) + VALUES[label]
        for lang, text in zip(LANGUAGES, texts):
            strings[lang][value_key(label)] = text
    return strings


def main():
    settings = build_settings()
    labels = {entry["label"] for tab in settings["tabs"] for section in tab["sections"]
              for entry in section["options"]}
    labels |= {tab["name"] for tab in settings["tabs"]}
    labels |= {section["title"] for tab in settings["tabs"] for section in tab["sections"]}
    missing = sorted(label for label in labels if label[len(P):] not in STRINGS)
    if missing:
        raise SystemExit(f"labels without strings: {missing}")

    SETTINGS.write_text(json.dumps(settings, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    for lang, entries in build_strings(settings).items():
        path = LANG_DIR / f"{lang}.json"
        current = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=OrderedDict)
        merged = OrderedDict((k, v) for k, v in current.items() if not k.startswith(P))
        merged.update(sorted(entries.items()))
        path.write_text(json.dumps(merged, indent=4, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"wrote {SETTINGS.relative_to(TICO.parent)} and {len(LANGUAGES)} languages")


if __name__ == "__main__":
    main()
