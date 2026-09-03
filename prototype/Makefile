CC = g++
CFLAGS = -std=c++17 -Wall -Wextra -O2
LDFLAGS = -lavformat -lavcodec -lavutil -lswscale -lpthread

# 如果有SDL2，可以启用预览
# LDFLAGS += -lSDL2

TARGETS = ffmpeg_capture_test test_device simple_preview v4l2_raw_test

.PHONY: all clean

all: $(TARGETS)

ffmpeg_capture_test: ffmpeg_capture_test.cpp
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_device: test_device.cpp
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

simple_preview: simple_preview.cpp
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

v4l2_raw_test: v4l2_raw_test.cpp
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGETS)

# 安装依赖（Ubuntu/Debian）
install-deps:
	sudo apt-get update
	sudo apt-get install -y libavformat-dev libavcodec-dev libavutil-dev libswscale-dev

# 安装依赖（Rockchip平台）
install-deps-rk:
	sudo apt-get update
	sudo apt-get install -y libavformat-dev libavcodec-dev libavutil-dev libswscale-dev ffmpeg

# 测试运行
test: ffmpeg_capture_test
	./ffmpeg_capture_test /dev/video0 test_output.mp4
