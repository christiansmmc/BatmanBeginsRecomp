"""What the port itself shows the player is in English: the options screen, its prompts and the message boxes."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FOLDERS = ("src", "sdk/runtime")
SUFFIXES = {".cpp", ".h", ".hpp", ".inl"}
SEPARATOR = "·"  # between a scale and its pixels, "2x · 1280 x 960"

# ASCII words of the Portuguese the port used to show; an accented word is caught by its letters
PORTUGUESE = re.compile(
    r"\b(para|de|da|das|na|nas|em|um|uma|ou|esta|este|deste|jogo|tela|janela|tecla|teclado|ligado|desligado|borda|"
    r"cheia|fechar|voltar|abas|selecionar|cancelar|pressione|aperte|segure|pular|soco|chute|gatilho|direcional|clique|"
    r"esq|dir|cima|baixo|esquerda|direita|andar|frente|modo|tamanho|filtro|textura|imagem|pasta|arquivo|programa|erro|"
    r"coloque|troque|houve|outra|americano|perdeu|envie|lado)\b",
    re.IGNORECASE)


def string_literals(source: str):
    """(line, text) of each string literal in C++ source, outside comments."""
    i, n, line = 0, len(source), 1
    while i < n:
        c = source[i]
        if c == "\n":
            line += 1
            i += 1
        elif source.startswith("//", i):
            end = source.find("\n", i)
            i = n if end < 0 else end
        elif source.startswith("/*", i):
            end = source.index("*/", i + 2)
            line += source.count("\n", i, end)
            i = end + 2
        elif c == '"' and re.search(r"(?<![A-Za-z0-9_])(u8|[uUL])?R$", source[max(0, i - 3):i]):
            open_paren = source.index("(", i)
            close = ")" + source[i + 1:open_paren] + '"'
            end = source.index(close, open_paren)
            text = source[open_paren + 1:end]
            yield line, text
            line += text.count("\n")
            i = end + len(close)
        elif c == '"':
            j = i + 1
            while source[j] != '"':
                j += 2 if source[j] == "\\" else 1
            yield line, source[i + 1:j]
            i = j + 1
        elif c == "'" and re.search(r"(?<![A-Za-z0-9_])\d[0-9A-Fa-fxXbB']*$", source[max(0, i - 40):i]):
            i += 1  # a digit separator, 0xFFFF'FFFF
        elif c == "'":
            j = i + 1
            while source[j] != "'":
                j += 2 if source[j] == "\\" else 1
            i = j + 1
        else:
            i += 1


def not_english(text: str) -> bool:
    return bool(PORTUGUESE.search(text)) or any(ord(c) > 127 for c in text.replace(SEPARATOR, ""))


def test_string_literals_skip_comments_and_characters():
    source = (
        '// "SEGURE A PARA PULAR"\n'
        'const char* a = "HOLD \\"A\\" TO SKIP"; /* "Vídeos" */\n'
        "char q = '\"'; uint32_t mask = 0xFFFF'FFFF;\n"
        'auto shader = R"hlsl(float4 main() { return "x"; })hlsl";\n'
        'const wchar_t* b = L"OK";\n'
    )
    assert list(string_literals(source)) == [
        (2, 'HOLD \\"A\\" TO SKIP'),
        (4, 'float4 main() { return "x"; }'),
        (5, "OK"),
    ]


def test_portuguese_is_told_from_english():
    for text in ("SEGURE A PARA PULAR", "TELA CHEIA", "DESLIGADO", "SHIFT ESQ", "OPÇÕES", "Houve um erro gráfico"):
        assert not_english(text), text
    for text in ("HOLD A TO SKIP", "FULLSCREEN", "OFF", "LEFT SHIFT", "OPTIONS", "2x · 1280 x 960", "logs\\\\batman.log"):
        assert not not_english(text), text


def test_the_ports_text_is_english():
    found = []
    for folder in FOLDERS:
        for path in sorted((ROOT / folder).rglob("*")):
            if path.is_file() and path.suffix in SUFFIXES:
                for line, text in string_literals(path.read_text(encoding="utf-8")):
                    if not_english(text):
                        found.append(f"{path.relative_to(ROOT).as_posix()}:{line}: {text}")
    assert found == []
