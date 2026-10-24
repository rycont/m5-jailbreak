#!/bin/sh
# DISABLED 2026-10-24 11:02 KST.
# The last "direct KDE" run was KWin with Compositing Type: QPainter.
# QPainter on the panel is forbidden. Do not enable this until the EGL
# client-extension path works and KWin reports Compositing Type: OpenGL.
echo "QPAINTER_DISABLED: refusing to start until OpenGL works" >&2
exit 1
