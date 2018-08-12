DPI_MODELS += camera

camera_SRCS = camera/camera.cpp

MAGICK=$(shell pkg-config --exists GraphicsMagick || echo FAILED)

ifeq '$(MAGICK)' ''
camera_CFLAGS += $(shell pkg-config GraphicsMagick --cflags)
camera_CFLAGS += -D__MAGICK__
camera_LDFLAGS += -lGraphicsMagick++ -lGraphicsMagick
#LDFLAGS += $(shell Magick++-config --ldflags)
endif
