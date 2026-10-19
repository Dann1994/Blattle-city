#!/usr/bin/env python
"""Publica una entrega de Battle City NES: compila, arma el instalador de PC y el APK, crea los releases en GitHub y listo.

Uso (desde la carpeta del proyecto, con Python 3):
    python tools/publish_release.py 1.1.0 [--notas "texto"] [--sin-tests] [--sin-push]

Que hace, en orden:
  1. cambia la version en src/app/version.h (1.1.0 -> BCNES_VERSION_CODE 10100);
  2. compila (build/), corre los tests y el oraculo de la ROM;
  3. arma dist/BattleCityNES-Setup-<v>.exe (instalador de PC), dist/BattleCityNES-PC.zip (version portable) y dist/BattleCityNES.apk (arm64);
  4. hace commit "Version <v>" y lo sube (git push);
  5. crea dos releases: pc-v<v> (instalador + zip) y android-v<v> (APK). El juego los busca por esas etiquetas.
Las credenciales de GitHub salen del administrador de credenciales de git (el mismo del push). Con --sin-push no toca GitHub (solo compila).
IMPORTANTE: el juego instalado compara su version con la del release mas nuevo: la version siempre tiene que SUBIR.
"""
import argparse, json, os, re, subprocess, sys, urllib.error, urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO = "Dann1994/Blattle-city"
MSYS = r"C:\msys64"
BASH = MSYS + r"\usr\bin\bash.exe"


def sh(cmd, **kw):
    """Corre un comando en bash de MSYS2 (con el compilador en el PATH)."""
    env = {**os.environ, "MSYSTEM": "MINGW64", "CHERE_INVOKING": "1"}
    print("$", cmd)
    subprocess.check_call([BASH, "-lc", "export PATH=/mingw64/bin:/usr/bin:$PATH; cd '%s' && %s" % (ROOT.replace("\\", "/"), cmd)], env=env, **kw)


def read_version():
    t = open(os.path.join(ROOT, "src", "app", "version.h"), encoding="utf-8").read()
    return re.search(r'BCNES_VERSION "([0-9.]+)"', t).group(1)


def set_version(v):
    parts = [int(x) for x in v.split(".")]
    while len(parts) < 3:
        parts.append(0)
    code = parts[0] * 10000 + parts[1] * 100 + parts[2]
    p = os.path.join(ROOT, "src", "app", "version.h")
    t = open(p, encoding="utf-8").read()
    t = re.sub(r'BCNES_VERSION "[0-9.]+"', 'BCNES_VERSION "%s"' % v, t)
    t = re.sub(r"BCNES_VERSION_CODE [0-9]+", "BCNES_VERSION_CODE %d" % code, t)
    open(p, "w", encoding="utf-8", newline="").write(t)
    return code


def cmp_version(a, b):
    pa = [int(x) for x in a.split(".")]
    pb = [int(x) for x in b.split(".")]
    return (pa > pb) - (pa < pb)


def token():
    out = subprocess.run(["git", "credential", "fill"], input="protocol=https\nhost=github.com\n\n", capture_output=True, text=True,
                         env={**os.environ, "GIT_TERMINAL_PROMPT": "0"}).stdout
    return [l.split("=", 1)[1] for l in out.splitlines() if l.startswith("password=")][0]


def api(tok, method, url, data=None, headers=None, raw=False):
    h = {"Authorization": "Bearer " + tok, "Accept": "application/vnd.github+json", "User-Agent": "bcnes-publish", "X-GitHub-Api-Version": "2022-11-28"}
    h.update(headers or {})
    body = data if raw else (json.dumps(data).encode() if data is not None else None)
    try:
        with urllib.request.urlopen(urllib.request.Request(url, data=body, method=method, headers=h), timeout=900) as r:
            txt = r.read()
            return json.loads(txt) if txt else {}
    except urllib.error.HTTPError as e:
        sys.exit("HTTP %d: %s" % (e.code, e.read().decode()[:400]))


def make_release(tok, tag, name, body, sha, files):
    r = api(tok, "POST", "https://api.github.com/repos/%s/releases" % REPO,
            {"tag_name": tag, "target_commitish": sha, "name": name, "body": body, "draft": False, "prerelease": False})
    for path, ctype in files:
        up = r["upload_url"].split("{")[0] + "?name=" + os.path.basename(path)
        a = api(tok, "POST", up, open(path, "rb").read(), {"Content-Type": ctype}, raw=True)
        print("  subido", a["browser_download_url"], a["size"])
    print("release", r["html_url"])


def main():
    ap = argparse.ArgumentParser(description="Publica una entrega de Battle City NES")
    ap.add_argument("version", help="version nueva, por ejemplo 1.1.0 (tiene que ser mayor que la actual)")
    ap.add_argument("--notas", default="", help="texto de las novedades (va en la descripcion de los releases)")
    ap.add_argument("--sin-tests", action="store_true")
    ap.add_argument("--sin-push", action="store_true", help="solo compila y arma los archivos (no commit ni GitHub)")
    ap.add_argument("--primera", action="store_true", help="publica la version que ya figura en version.h (la primera entrega con version)")
    a = ap.parse_args()
    if not re.fullmatch(r"[0-9]+(\.[0-9]+){1,2}", a.version):
        sys.exit("la version tiene que ser como 1.1.0")
    old = read_version()
    if cmp_version(a.version, old) <= 0 and not (a.primera and a.version == old):
        sys.exit("la version nueva (%s) tiene que ser mayor que la actual (%s)" % (a.version, old))
    set_version(a.version)
    dist = os.path.join(ROOT, "dist")
    sh("cmake -S . -B build > /dev/null && cmake --build build")
    if not a.sin_tests:
        sh("./build/bin/nes_tests.exe | tail -2 && (cd build/bin && ./rom_oracle_tests.exe | tail -1)")
    # carpeta del juego (portable) + instalador + zip + APK
    sh("mkdir -p dist/BattleCityNES && cp build/bin/battlecitynes.exe build/bin/level_editor.exe dist/BattleCityNES/")
    sh("bash installer/build_installer.sh")
    sh("rm -f dist/BattleCityNES-PC.zip && powershell -NoProfile -Command \"Compress-Archive -Path 'dist/BattleCityNES' -DestinationPath 'dist/BattleCityNES-PC.zip' -Force\"")
    sh("bash android/build_apk.sh")
    files_pc = [(os.path.join(dist, "BattleCityNES-Setup-%s.exe" % a.version), "application/octet-stream"), (os.path.join(dist, "BattleCityNES-PC.zip"), "application/zip")]
    files_an = [(os.path.join(dist, "BattleCityNES.apk"), "application/vnd.android.package-archive")]
    for f, _ in files_pc + files_an:
        if not os.path.exists(f):
            sys.exit("falta " + f)
    if a.sin_push:
        print("Listo (sin publicar). Archivos en", dist)
        return
    subprocess.check_call(["git", "-C", ROOT, "add", "-A"])                      # todo lo del proyecto (build/ y dist/ estan en .gitignore)
    subprocess.call(["git", "-C", ROOT, "-c", "user.name=dani", "-c", "user.email=danijdv1994@gmail.com", "commit", "-q", "-m", "Version %s: %s" % (a.version, (a.notas or "entrega")[:120]), "-m", "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"])
    subprocess.check_call(["git", "-C", ROOT, "push", "origin", "HEAD"])
    sha = subprocess.check_output(["git", "-C", ROOT, "rev-parse", "HEAD"], text=True).strip()
    tok = token()
    notas = a.notas or "Entrega %s." % a.version
    make_release(tok, "pc-v" + a.version, "Battle City NES %s - PC (Windows)" % a.version,
                 notas + "\n\nInstalador (`BattleCityNES-Setup-%s.exe`) o version portable (`BattleCityNES-PC.zip`). El juego instalado busca y aplica las actualizaciones solo." % a.version, sha, files_pc)
    make_release(tok, "android-v" + a.version, "Battle City NES %s - Android" % a.version,
                 notas + "\n\nAPK para Android 7.0 o mas nuevo (arm64). El juego instalado busca las actualizaciones al abrir y pide confirmar la instalacion.", sha, files_an)
    print("Entrega %s publicada." % a.version)


if __name__ == "__main__":
    main()
