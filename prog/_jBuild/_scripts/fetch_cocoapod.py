#!/usr/bin/env python3
"""Fetch a binary CocoaPods pod into the build cache (see iOS/cocoapods.jam).

Downloads the archive referenced by the pod's podspec 'source' into
<cache>/<pod>/<version>/src and populates <cache>/<pod>/<version>/<slice>
with symlinks to the *.framework / *.bundle / *.a entries matching the
requested platform slice (xcframework slices are resolved via Info.plist).
Writes a .jam-ready stamp on success; jam targets depend on that stamp.

Safe to run concurrently for different slices of the same pod: the src
tree is extracted into a temp dir and published with an atomic rename.
"""

import argparse
import fcntl
import hashlib
import io
import json
import os
import plistlib
import posixpath
import random
import re
import shutil
import ssl
import stat
import subprocess
import sys
import tarfile
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
import zipfile

SPEC_URL = "https://trunk.cocoapods.org/api/v1/pods/{pod}/specs/{version}"
USER_AGENT = "dagor-jam-cocoapods/1.0"
RETRIES = 4
RETRY_DEADLINE = 400
_START = time.monotonic()

RESOLVER_VERSION = 2


def out_of_retry_budget():
    return time.monotonic() - _START > RETRY_DEADLINE


def redact_url(url):
    parts = urllib.parse.urlsplit(url)
    netloc = parts.hostname or ""
    if parts.port:
        netloc = "%s:%d" % (netloc, parts.port)
    redacted = urllib.parse.urlunsplit((parts.scheme, netloc, parts.path, "", ""))
    if parts.query:
        redacted += "?<redacted>"
    return redacted
RETRIABLE_HTTP = (429, 500, 502, 503, 504)


def log(msg):
    print("fetch_cocoapod: %s" % msg, flush=True)


def fail(msg):
    log("ERROR: %s" % msg)
    sys.exit(1)


def _ssl_contexts():
    yield None
    try:
        import certifi
        yield ssl.create_default_context(cafile=certifi.where())
    except ImportError:
        pass
    for pem in ("/etc/ssl/cert.pem", "/etc/ssl/certs/ca-certificates.crt"):
        if os.path.exists(pem):
            yield ssl.create_default_context(cafile=pem)


def _curl_get(url, dest_path):
    curl = shutil.which("curl")
    if not curl:
        return False
    cmd = [curl, "-fsSL", "--retry", "2",
           "--connect-timeout", "30",
           "--speed-limit", "1024", "--speed-time", "60",
           "--max-time", "1800",
           "-o", dest_path, url]
    try:
        return subprocess.run(cmd, timeout=1900).returncode == 0
    except subprocess.TimeoutExpired:
        log("curl timed out downloading %s" % redact_url(url))
        return False


def _http_get_once(url, dest_path=None):
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    last_err = None
    for ctx in _ssl_contexts():
        try:
            with urllib.request.urlopen(req, timeout=300, context=ctx) as resp:
                expected = resp.headers.get("Content-Length")
                if dest_path is None:
                    data = resp.read()
                    got = len(data)
                else:
                    with open(dest_path, "wb") as f:
                        shutil.copyfileobj(resp, f, length=1 << 20)
                    got = os.path.getsize(dest_path)
                if expected is not None and got != int(expected):
                    raise urllib.error.URLError(
                        "truncated download: got %d of %s bytes" % (got, expected))
                return data if dest_path is None else None
        except urllib.error.URLError as e:
            last_err = e
            if not isinstance(getattr(e, "reason", None), ssl.SSLError):
                raise
    # cert store is broken for this python; curl uses the OS trust store
    log("urllib failed (%s), retrying with curl" % last_err)
    tmp = dest_path or (tempfile.mktemp(prefix="fetch_cocoapod."))
    if _curl_get(url, tmp):
        if dest_path is None:
            with open(tmp, "rb") as f:
                data = f.read()
            os.remove(tmp)
            return data
        return None
    raise last_err


def http_get(url, dest_path=None):
    for attempt in range(RETRIES):
        try:
            return _http_get_once(url, dest_path)
        except urllib.error.HTTPError as e:
            if e.code not in RETRIABLE_HTTP or attempt == RETRIES - 1:
                raise
            if out_of_retry_budget():
                log("retry budget exhausted for %s" % redact_url(url))
                raise
        except urllib.error.URLError:
            if attempt == RETRIES - 1:
                raise
            if out_of_retry_budget():
                log("retry budget exhausted for %s" % redact_url(url))
                raise
        delay = 2 ** attempt + random.uniform(0, 2)
        log("retrying %s in %.1fs" % (redact_url(url), delay))
        time.sleep(delay)


def get_podspec(pod, version):
    shard = hashlib.md5(pod.encode()).hexdigest()[:3]
    urls = [
        "https://cdn.cocoapods.org/Specs/%s/%s/%s/%s/%s/%s.podspec.json"
        % (shard[0], shard[1], shard[2], pod, version, pod),
        SPEC_URL.format(pod=pod, version=version),
    ]
    last_err = None
    for url in urls:
        try:
            return json.loads(http_get(url))
        except Exception as e:
            last_err = "%s: %s" % (url, e)
            log("podspec fetch failed, %s" % last_err)
    fail("cannot get podspec %s/%s: %s" % (pod, version, last_err))


def archive_url_from_source(src, version):
    # Return (url, kind) for the podspec 'source' dict.
    if not isinstance(src, dict):
        return None
    if "http" in src:
        return src["http"]
    if "git" in src:
        git = src["git"]
        ref = src.get("commit") or src.get("tag") or version
        m = re.search(r"github\.com[:/](.+?)(?:\.git)?/*$", git)
        if m:
            return "https://codeload.github.com/%s/zip/%s" % (m.group(1), ref)
        m = re.search(r"bitbucket\.org[:/](.+?)(?:\.git)?/*$", git)
        if m:
            return "https://bitbucket.org/%s/get/%s.zip" % (m.group(1), ref)
        fail("unsupported git host in podspec source: %s" % git)
    return None


def _entry_escapes(name, link_target=None):
    parts = [p for p in name.replace("\\", "/").split("/") if p not in ("", ".")]
    if name.startswith("/") or ".." in parts:
        return True
    if link_target is not None:
        if link_target.startswith("/"):
            return True
        resolved = posixpath.normpath(posixpath.join(posixpath.dirname(name), link_target))
        if resolved == ".." or resolved.startswith("../"):
            return True
    return False


def _check_zip_entries(archive_path):
    with zipfile.ZipFile(archive_path) as zf:
        for info in zf.infolist():
            target = None
            if info.create_system == 3 and stat.S_ISLNK(info.external_attr >> 16):
                target = zf.read(info).decode(errors="replace")
            if _entry_escapes(info.filename, target):
                fail("unsafe entry in %s: %s" % (os.path.basename(archive_path), info.filename))


def _check_tar_members(tf, archive_path):
    for m in tf.getmembers():
        target = m.linkname if (m.issym() or m.islnk()) else None
        if _entry_escapes(m.name, target):
            fail("unsafe entry in %s: %s" % (os.path.basename(archive_path), m.name))


def extract_archive(archive_path, dest_dir):
    name = archive_path.lower()
    if name.endswith((".tar.gz", ".tgz", ".tar.bz2", ".tar.xz", ".txz", ".tar")):
        with tarfile.open(archive_path) as tf:
            try:
                tf.extractall(dest_dir, filter="data")
            except TypeError:  # old python without the filter argument
                _check_tar_members(tf, archive_path)
                tf.extractall(dest_dir)
            except getattr(tarfile, "FilterError", ()) as e:
                # unsafe entry, not a corrupted download - do not retry
                fail("unsafe entry in %s: %s" % (os.path.basename(archive_path), e))
        return
    _check_zip_entries(archive_path)
    # zip: prefer ditto/unzip which preserve symlinks inside frameworks
    for cmd in (["/usr/bin/ditto", "-x", "-k", archive_path, dest_dir],
                ["unzip", "-q", archive_path, "-d", dest_dir]):
        if shutil.which(cmd[0]):
            r = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
            if r.returncode == 0:
                return
            log("%s failed (%s), trying next extractor" % (cmd[0], r.stderr.decode(errors="replace").strip()))
    with zipfile.ZipFile(archive_path) as zf:
        zf.extractall(dest_dir)


def check_source_marker(prebuilt_src, pod, version, source_url):
    marker_path = os.path.join(prebuilt_src, ".source.json")
    try:
        with open(marker_path) as f:
            marker = json.load(f)
    except OSError:
        log("WARNING: using unverified prebuilt package %s (no .source.json); "
            "populate the mirror by copying a downloaded src dir" % prebuilt_src)
        return
    except ValueError:
        fail("corrupt .source.json in prebuilt package %s" % prebuilt_src)

    if (marker.get("pod"), marker.get("version")) != (pod, version):
        fail("prebuilt package %s was made from %s/%s, but %s/%s is requested"
             % (prebuilt_src, marker.get("pod"), marker.get("version"), pod, version))
    if source_url and marker.get("url") != source_url:
        fail("prebuilt package %s was downloaded from %s, but the build pins %s; "
             "refresh the mirror entry"
             % (prebuilt_src, redact_url(marker.get("url") or ""), redact_url(source_url)))
    log("using prebuilt package %s (source %s)"
        % (prebuilt_src, redact_url(marker.get("url") or "")))


def ensure_src(pod_dir, pod, version, source_url=None, prebuilt_dir=None,
               no_download=False):
    src_dir = os.path.join(pod_dir, "src")
    if os.path.isdir(src_dir):
        return src_dir

    if prebuilt_dir:
        prebuilt_src = os.path.join(prebuilt_dir, pod, version, "src")
        if os.path.isdir(prebuilt_src):
            check_source_marker(prebuilt_src, pod, version, source_url)
            return prebuilt_src

    if no_download:
        fail("downloads from pod repositories are disabled and %s/%s is not in "
             "the prebuilt dir (%s); either populate %s/%s/%s/src or build with "
             "-sCocoaPodsAllowDownload=yes"
             % (pod, version, prebuilt_dir or "<unset>",
                prebuilt_dir or "<prebuilt-dir>", pod, version))

    spec = {}
    url = source_url
    if not url:
        spec = get_podspec(pod, version)
        url = archive_url_from_source(spec.get("source"), version)
    if not url:
        fail("podspec of %s/%s has no downloadable source (source=%r); "
             "binary pods with vendored frameworks are required" % (pod, version, spec.get("source")))

    os.makedirs(pod_dir, exist_ok=True)
    tmp_dir = tempfile.mkdtemp(prefix="src.tmp.", dir=pod_dir)
    try:
        archive_name = os.path.basename(url.split("?")[0]) or "archive.zip"
        archive_path = os.path.join(tmp_dir, "__" + archive_name)
        extract_dir = os.path.join(tmp_dir, "x")
        for attempt in range(RETRIES):
            log("downloading %s" % redact_url(url))
            http_get(url, archive_path)
            shutil.rmtree(extract_dir, ignore_errors=True)
            os.makedirs(extract_dir)
            try:
                extract_archive(archive_path, extract_dir)
                break
            except Exception as e:
                os.remove(archive_path)
                if attempt == RETRIES - 1 or out_of_retry_budget():
                    fail("cannot extract %s for %s/%s: %s" % (archive_name, pod, version, e))
                delay = 2 ** (attempt + 1) + random.uniform(0, 3)
                log("archive is corrupted (%s), re-downloading in %.1fs" % (e, delay))
                time.sleep(delay)
        os.remove(archive_path)
        with open(os.path.join(extract_dir, ".podspec.json"), "w") as f:
            json.dump(spec, f, indent=2)
        with open(os.path.join(extract_dir, ".source.json"), "w") as f:
            json.dump({"pod": pod, "version": version, "url": url}, f, indent=1)
        try:
            os.rename(extract_dir, src_dir)
        except OSError:
            if not os.path.isdir(src_dir):  # lost the race for a different reason
                raise
    finally:
        shutil.rmtree(tmp_dir, ignore_errors=True)
    return src_dir


def slice_matches(lib, platform, arch, variant):
    if lib.get("SupportedPlatform") != platform:
        return False
    if (lib.get("SupportedPlatformVariant") or "") != (variant or ""):
        return False
    return arch in lib.get("SupportedArchitectures", [])


def add_bundles_under(root, add):
    for r, dirs, _files in os.walk(root, followlinks=False):
        for d in list(dirs):
            if d.endswith(".bundle"):
                dirs.remove(d)
                add(d, os.path.join(r, d))


def vendored_paths_from_podspec(spec, platform):
    paths = []

    def take(d):
        for key in ("vendored_frameworks", "vendored_libraries"):
            v = d.get(key)
            if isinstance(v, str):
                paths.append(v)
            elif isinstance(v, list):
                paths.extend(v)

    def scan(d):
        take(d)
        plat = d.get(platform)
        if isinstance(plat, dict):
            take(plat)

    scan(spec)
    for sub in spec.get("subspecs") or []:
        if isinstance(sub, dict):
            scan(sub)
    return paths


def add_artifact_at(path, platform, arch, variant, add):
    if path.endswith(".xcframework") and os.path.isdir(path):
        info = os.path.join(path, "Info.plist")
        if not os.path.isfile(info):
            log("skipping %s: no Info.plist" % path)
            return
        with open(info, "rb") as f:
            plist = plistlib.load(f)
        for lib in plist.get("AvailableLibraries", []):
            if not slice_matches(lib, platform, arch, variant):
                continue
            lib_dir = os.path.join(path, lib["LibraryIdentifier"])
            lib_path = os.path.join(lib_dir, lib["LibraryPath"])
            add(os.path.basename(lib_path), lib_path)
            add_bundles_under(lib_dir, add)
            break
    elif path.endswith(".framework") and os.path.isdir(path):
        add(os.path.basename(path), path)  # plain (fat) framework
        add_bundles_under(path, add)
    elif path.endswith(".a") and os.path.isfile(path):
        add(os.path.basename(path), path)


def select_linkage_variant(path, linkage, src_dir):
    if linkage == "dynamic":
        want, other = "Dynamic", "Static"
    else:  # "auto" (default, prefer static) or explicit "static"
        want, other = "Static", "Dynamic"
    seg = os.sep + other + os.sep
    if seg in path:
        alt = path.replace(seg, os.sep + want + os.sep)
        if os.path.exists(alt):
            log("linkage=%s: using %s" % (linkage, os.path.relpath(alt, src_dir)))
            return alt
        if linkage != "auto":
            log("linkage=%s requested but no %s variant found for %s"
                % (linkage, want, os.path.relpath(path, src_dir)))
    return path


def find_artifacts(src_dir, platform, arch, variant, spec=None, linkage="auto"):
    import glob as _glob

    found = {}
    vendored_names = set()

    def add_vendored(name, path):
        found.setdefault(name, path)

    def add(name, path):
        if name in vendored_names:
            return
        # archives may carry both variants; we link statically, prefer static
        prev = found.get(name)
        if prev is None:
            found[name] = path
        elif "static" in path.lower() and "static" not in prev.lower():
            log("preferring static variant of %s" % name)
            found[name] = path

    for rel in vendored_paths_from_podspec(spec or {}, platform):
        # archives may unpack with one wrapper dir (github tag zips)
        matches = (_glob.glob(os.path.join(_glob.escape(src_dir), rel), recursive=True)
                   or _glob.glob(os.path.join(_glob.escape(src_dir), "*", rel), recursive=True))
        if not matches:
            log("vendored path not found in archive: %s" % rel)
        for m in sorted(matches):
            m = select_linkage_variant(m, linkage, src_dir)
            add_artifact_at(m, platform, arch, variant, add_vendored)
    vendored_names = set(found)
    if vendored_names:
        log("vendored artifacts: %s" % ", ".join(sorted(vendored_names)))

    for root, dirs, _files in os.walk(src_dir, followlinks=False):
        for d in list(dirs):
            path = os.path.join(root, d)
            if d.endswith(".xcframework") or d.endswith(".framework"):
                dirs.remove(d)
                add_artifact_at(path, platform, arch, variant, add)
            elif d.endswith(".bundle"):
                dirs.remove(d)
                add(d, path)
        for fn in _files:
            if fn.endswith(".a"):
                add(fn, os.path.join(root, fn))
    return found


def slice_lock(slice_dir):
    lf = open(slice_dir + ".lock", "w")
    fcntl.flock(lf, fcntl.LOCK_EX)
    return lf  # keep the fd open for the lifetime of the critical section


def populate_slice_dir(slice_dir, artifacts):
    # sweep tmp dirs orphaned by killed processes (we hold the slice lock)
    parent = os.path.dirname(slice_dir)
    for entry in os.listdir(parent):
        if entry.startswith(os.path.basename(slice_dir) + ".tmp."):
            shutil.rmtree(os.path.join(parent, entry), ignore_errors=True)

    tmp_dir = slice_dir + ".tmp.%d" % os.getpid()
    os.makedirs(tmp_dir)
    try:
        for name, path in sorted(artifacts.items()):
            os.symlink(os.path.relpath(path, slice_dir), os.path.join(tmp_dir, name))
        shutil.rmtree(slice_dir, ignore_errors=True)
        os.rename(tmp_dir, slice_dir)
    except BaseException:
        shutil.rmtree(tmp_dir, ignore_errors=True)
        raise


def check_expected_frameworks(expected, available, pod, version):
    missing = [fw for fw in expected if fw + ".framework" not in available]
    if missing:
        fail("pod %s/%s does not provide framework(s): %s; available: %s"
             % (pod, version, ", ".join(missing), ", ".join(sorted(available))))


def resolved_linkage(artifacts):
    for p in artifacts.values():
        if os.sep + "Static" + os.sep in p:
            return "static"
        if os.sep + "Dynamic" + os.sep in p:
            return "dynamic"
    return None


def check_stamp(stamp, args):
    if not os.path.isfile(stamp):
        return False
    try:
        with open(stamp) as f:
            data = json.load(f)
    except (OSError, ValueError):
        return False  # unreadable stamp, rebuild the slice
    if data.get("resolver") != RESOLVER_VERSION:
        return False
    check_expected_frameworks(args.frameworks, data.get("artifacts", []),
                              args.pod, args.pod_version)
    return True


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--pod", required=True)
    p.add_argument("--pod-version", required=True)
    p.add_argument("--cache-dir", required=True)
    p.add_argument("--platform", default="ios", choices=["ios", "tvos"])
    p.add_argument("--arch", default="arm64")
    p.add_argument("--variant", default=None, choices=[None, "simulator", "maccatalyst"])
    p.add_argument("--slice", required=True,
                   help="cache slice dir name; computed once by cocoapods.jam "
                        "(single source of truth, incl. any linkage suffix). "
                        "platform/arch/variant are still used to resolve the "
                        "matching xcframework slice.")
    p.add_argument("--frameworks", nargs="*", default=[],
                   help="framework names the build expects to link (validated)")
    p.add_argument("--linkage", default="auto", choices=["auto", "static", "dynamic"],
                   help="for pods shipping both Static/ and Dynamic/ framework "
                        "variants: 'auto' (default) and 'static' prefer static, "
                        "'dynamic' forces the dynamic variant")
    p.add_argument("--source-url", default=None,
                   help="download this archive instead of the podspec source")
    p.add_argument("--prebuilt-dir", default=None,
                   help="use <dir>/<pod>/<version>/src instead of downloading, if present")
    p.add_argument("--no-download", action="store_true",
                   help="forbid fetching from pod repositories; only the prebuilt dir "
                        "or an already populated cache may be used")
    args = p.parse_args()

    slice_id = args.slice

    pod_dir = os.path.join(args.cache_dir, args.pod, args.pod_version)
    slice_dir = os.path.join(pod_dir, slice_id)
    stamp = os.path.join(slice_dir, ".jam-ready")
    if check_stamp(stamp, args):
        return

    os.makedirs(pod_dir, exist_ok=True)
    lock = slice_lock(slice_dir)
    if check_stamp(stamp, args):  # done by a concurrent invocation while we waited
        return

    src_dir = ensure_src(pod_dir, args.pod, args.pod_version, args.source_url,
                         args.prebuilt_dir, args.no_download)
    spec = None
    spec_path = os.path.join(src_dir, ".podspec.json")
    if os.path.isfile(spec_path):
        with open(spec_path) as f:
            spec = json.load(f)
    artifacts = find_artifacts(src_dir, args.platform, args.arch, args.variant, spec, args.linkage)
    if not artifacts:
        fail("no frameworks/bundles/libs for slice %s found in %s" % (slice_id, src_dir))

    check_expected_frameworks(args.frameworks, artifacts, args.pod, args.pod_version)

    populate_slice_dir(slice_dir, artifacts)
    with open(stamp, "w") as f:
        json.dump({"pod": args.pod, "version": args.pod_version,
                   "slice": slice_id, "linkage": args.linkage,
                   "resolved": resolved_linkage(artifacts),
                   "resolver": RESOLVER_VERSION,
                   "artifacts": sorted(artifacts)}, f, indent=1)
    log("%s/%s [%s]: %s" % (args.pod, args.pod_version, slice_id, ", ".join(sorted(artifacts))))


if __name__ == "__main__":
    main()
