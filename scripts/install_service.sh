#!/usr/bin/env bash
# 安装开机自启动 systemd 服务
# 用法: sudo scripts/install_service.sh  (devices: /home/cat/yunlu)
set -e
cd "$(dirname "$0")/.."

APP_DIR="${APP_DIR:-/home/cat/yunlu}"
SVC="scripts/yunlu.service"

echo "==> 复制二进制到 $APP_DIR"
sudo install -d "$APP_DIR/recordings"
sudo cp -f build/yunlu "$APP_DIR/yunlu"
sudo chmod +x "$APP_DIR/yunlu"

echo "==> 安装 systemd 服务"
sudo cp -f "$SVC" /etc/systemd/system/yunlu.service
sudo systemctl daemon-reload
sudo systemctl enable yunlu.service

echo "==> 开机自启动已启用。立即启动: sudo systemctl start yunlu"
echo "==> 查看日志: journalctl -u yunlu -f"
