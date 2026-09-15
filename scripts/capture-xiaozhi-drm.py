#!/usr/bin/env python3
"""Read one active Xiaozhi software DRM framebuffer into a P6 PPM.

This diagnostic only supports Linux, linear 32-bit dumb buffers, and the
installed /usr/bin/lilygo-ui-xiaozhi process. It does not call SET_MASTER,
modeset, create/destroy-buffer, framebuffer-write, or process-control APIs.
The DRM debugfs clients table must already be available to verify ownership.
"""

import argparse
import ctypes as C
import errno
import fcntl
import json
import mmap
import os
from pathlib import Path
import struct
import sys


U32 = C.c_uint32
U64 = C.c_uint64


class Resources(C.Structure):
    _fields_ = [(name, field) for kind in ("fbs", "crtcs", "connectors", "encoders")
                for name, field in (("count_" + kind, C.c_int), (kind, C.POINTER(U32)))] + [
                    (name, U32) for name in ("min_width", "max_width", "min_height", "max_height")]


class Mode(C.Structure):
    _fields_ = [("clock", U32)] + [(name, C.c_uint16) for name in (
        "hdisplay", "hsync_start", "hsync_end", "htotal", "hskew",
        "vdisplay", "vsync_start", "vsync_end", "vtotal", "vscan")] + [
            ("vrefresh", U32), ("flags", U32), ("type", U32), ("name", C.c_char * 32)]


class Crtc(C.Structure):
    _fields_ = [(name, U32) for name in ("crtc_id", "buffer_id", "x", "y", "width", "height")] + [
        ("mode_valid", C.c_int), ("mode", Mode), ("gamma_size", C.c_int)]


class Framebuffer(C.Structure):
    # Exact drmModeFB2 ABI from the target SDK's xf86drmMode.h.
    _fields_ = [(name, U32) for name in ("fb_id", "width", "height", "pixel_format")] + [
        ("modifier", U64), ("flags", U32), ("handles", U32 * 4),
        ("pitches", U32 * 4), ("offsets", U32 * 4)]


def fail(message):
    raise RuntimeError(message)


def check_application(pid, card, expected_start=None):
    proc = Path("/proc") / str(pid)
    if os.readlink(proc / "exe") != "/usr/bin/lilygo-ui-xiaozhi":
        fail("The requested PID is not the installed Xiaozhi executable")
    # comm can contain spaces; field 22 is the twentieth field after its ')'.
    start = (proc / "stat").read_text().rsplit(")", 1)[1].split()[19]
    if expected_start is not None and start != expected_start:
        fail("The Xiaozhi PID was reused during capture")
    owns_card = False
    for entry in (proc / "fd").iterdir():
        try:
            owns_card |= os.path.realpath(entry) == str(card)
        except FileNotFoundError:
            continue
    if not owns_card:
        fail("The Xiaozhi process has no open descriptor for the selected DRM card")

    minor = os.minor(card.stat().st_rdev)
    clients = Path(f"/sys/kernel/debug/dri/{minor}/clients")
    try:
        lines = clients.read_text().splitlines()
    except OSError as error:
        fail(f"Cannot verify DRM master ownership from {clients}: {error}; no frame was captured")
    header = lines[0].lower().split() if lines else []
    process_column = "tgid" if "tgid" in header else "pid"
    if process_column not in header or "master" not in header:
        fail("Unrecognized DRM clients table; cannot verify which app owns scanout")
    pid_column, master_column = header.index(process_column), header.index("master")
    masters = []
    for line in lines[1:]:
        fields = line.split()
        if len(fields) <= max(pid_column, master_column):
            continue
        if fields[master_column].lower() in ("y", "yes", "1"):
            try:
                masters.append(int(fields[pid_column]))
            except ValueError:
                fail("Unrecognized PID in DRM clients table")
    if set(masters) != {pid}:
        fail(f"Xiaozhi PID {pid} is not the sole active DRM master; refusing capture")
    return start


def load_drm():
    drm = C.CDLL("libdrm.so.2", use_errno=True)
    for name, structure, arguments in (
        ("Resources", Resources, [C.c_int]),
        ("Crtc", Crtc, [C.c_int, U32]),
        ("FB2", Framebuffer, [C.c_int, U32]),
    ):
        get = getattr(drm, "drmModeGet" + name)
        get.argtypes, get.restype = arguments, C.POINTER(structure)
        free = getattr(drm, "drmModeFree" + name)
        free.argtypes, free.restype = [C.POINTER(structure)], None
    return drm


def current_crtc(drm, fd, crtc_id):
    pointer = drm.drmModeGetCrtc(fd, crtc_id)
    if not pointer:
        raise OSError(C.get_errno() or errno.EIO, "drmModeGetCrtc failed")
    try:
        return Crtc.from_buffer_copy(C.string_at(pointer, C.sizeof(Crtc)))
    finally:
        drm.drmModeFreeCrtc(pointer)


def read_pixels(drm, fd, crtc):
    pointer = drm.drmModeGetFB2(fd, crtc.buffer_id)
    if not pointer:
        raise OSError(C.get_errno() or errno.EIO, "drmModeGetFB2 failed")
    handles = set()
    try:
        fb = pointer.contents
        handles = {int(handle) for handle in fb.handles if handle}
        if not fb.handles[0]:
            fail("GetFB2 did not expose a GEM handle; run with the required root/CAP_SYS_ADMIN permission")
        if any(fb.handles[1:]) or any(fb.pitches[1:]) or any(fb.offsets):
            fail("Only a single-plane framebuffer with zero offsets is supported")
        if fb.modifier != 0 or fb.pixel_format not in (0x34325258, 0x34325241):
            fail("Only linear DRM_FORMAT_XRGB8888/ARGB8888 is supported")
        if (fb.width, fb.height) != (crtc.width, crtc.height) or crtc.x or crtc.y:
            fail("Scaled/cropped/offset CRTCs are not supported")
        pitch = int(fb.pitches[0])
        length = pitch * int(fb.height)
        if not 0 < fb.width <= 8192 or not 0 < fb.height <= 8192 or pitch < fb.width * 4 or length > 64 * 1024 * 1024:
            fail("Invalid or unexpectedly large framebuffer geometry")
        # drm_mode_map_dumb { uint32 handle, pad; uint64 offset; }, Linux _IOWR.
        request = bytearray(struct.pack("=IIQ", fb.handles[0], 0, 0))
        fcntl.ioctl(fd, 0xC01064B3, request, True)
        offset = struct.unpack("=IIQ", request)[2]
        if offset % mmap.PAGESIZE:
            fail("The dumb-buffer mmap offset is not page aligned")
        with mmap.mmap(fd, length, flags=mmap.MAP_SHARED, prot=mmap.PROT_READ, offset=offset) as mapped:
            pixels = mapped[:]
        metadata = {"crtc": int(crtc.crtc_id), "framebuffer": int(fb.fb_id),
                    "width": int(fb.width), "height": int(fb.height), "pitch": pitch,
                    "format": int(fb.pixel_format).to_bytes(4, "little").decode("ascii")}
        return pixels, metadata
    finally:
        # GetFB2 creates references in this new DRM fd, not in the application's fd.
        for handle in handles:
            fcntl.ioctl(fd, 0x40086409, struct.pack("=II", handle, 0))  # DRM_IOCTL_GEM_CLOSE
        drm.drmModeFreeFB2(pointer)


def ppm_bytes(pixels, width, height, pitch):
    result = bytearray(f"P6\n{width} {height}\n255\n".encode("ascii"))
    for y in range(height):
        row = pixels[y * pitch:y * pitch + width * 4]
        rgb = bytearray(width * 3)
        rgb[0::3], rgb[1::3], rgb[2::3] = row[2::4], row[1::4], row[0::4]
        result.extend(rgb)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pid", type=int, required=True, help="Verified current Xiaozhi application PID")
    parser.add_argument("--card", type=Path, default=Path("/dev/dri/card0"))
    parser.add_argument("--crtc", type=int, help="Required if more than one CRTC is active")
    parser.add_argument("--output", type=Path, required=True, help="New P6 PPM output file (never overwrites)")
    args = parser.parse_args()
    if sys.platform != "linux" or sys.byteorder != "little" or C.sizeof(C.c_void_p) != 8:
        fail("This diagnostic supports only 64-bit little-endian Linux")
    if args.pid <= 0 or args.output.exists():
        fail("A valid PID and a new output path are required")
    card = args.card.resolve(strict=True)
    start = check_application(args.pid, card)
    drm = load_drm()
    fd = os.open(card, os.O_RDONLY | os.O_CLOEXEC)
    try:
        check_application(args.pid, card, start)
        resources = drm.drmModeGetResources(fd)
        if not resources:
            raise OSError(C.get_errno() or errno.EIO, "drmModeGetResources failed")
        try:
            ids = list(resources.contents.crtcs[:resources.contents.count_crtcs])
        finally:
            drm.drmModeFreeResources(resources)
        active = [current_crtc(drm, fd, identity) for identity in ids if args.crtc is None or identity == args.crtc]
        active = [crtc for crtc in active if crtc.mode_valid and crtc.buffer_id]
        if len(active) != 1:
            fail("Exactly one active CRTC is required; select it with --crtc")
        for attempt in range(3):
            before = current_crtc(drm, fd, active[0].crtc_id)
            check_application(args.pid, card, start)
            pixels, metadata = read_pixels(drm, fd, before)
            after = current_crtc(drm, fd, before.crtc_id)
            check_application(args.pid, card, start)
            if (before.buffer_id, before.width, before.height) == (after.buffer_id, after.width, after.height):
                break
        else:
            fail("Scanout changed during all three reads; no stable frame was captured")
        output = ppm_bytes(pixels, metadata["width"], metadata["height"], metadata["pitch"])
        with args.output.open("xb") as stream:
            stream.write(output)
        print(json.dumps({**metadata, "pid": args.pid, "output": str(args.output),
                          "capture": "active software DRM buffer; physical panel output not measured"}))
    finally:
        os.close(fd)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError, AttributeError) as error:
        print(f"Capture failed: {error}", file=sys.stderr)
        sys.exit(1)
