# PlatformIO post-script: size trimming of the link step (R2).
# The PlatformIO ESP8266 framework adds "-u _printf_float -u _scanf_float", which pulls the floating point
# printf/scanf and strtod/dtoa code (about 14 KB) into every build. GeekQuest never prints or scans floating
# point numbers (tools/lint_safety.py rule L9 forbids "%f" and friends), so those forced references are removed.
# The libc references to them are weak: if something did need them the link would still succeed.
Import("env")  # noqa: F821

DROP = {"_printf_float", "_scanf_float"}


def words(flag):
    """'-u', '-Wl,-u,_printf_float' or ('-u', '_x') as a list of words."""
    if isinstance(flag, (tuple, list)):
        return [str(x) for x in flag]
    return str(flag).replace("-Wl,", "").replace(",", " ").split()


def slim(flags):
    flags = list(flags)
    out, dropped, i = [], [], 0
    while i < len(flags):
        w = words(flags[i])
        if len(w) == 2 and w[0] == "-u" and w[1] in DROP:          # one flag holding both words
            dropped.append(w[1])
        elif w == ["-u"] and i + 1 < len(flags) and words(flags[i + 1]) and words(flags[i + 1])[0] in DROP:
            dropped.append(words(flags[i + 1])[0])                 # "-u" followed by the symbol
            i += 1
        else:
            out.append(flags[i])
        i += 1
    return out, dropped


new_flags, gone = slim(env.get("LINKFLAGS", []))  # noqa: F821
env.Replace(LINKFLAGS=new_flags)  # noqa: F821
print("pio_slim: removed link flags:", gone if gone else "none found (nothing to remove)")
