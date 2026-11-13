TARGET := iphone:clang:latest:15.0
ARCHS = arm64
include $(THEOS)/makefiles/common.mk
LIBRARY_NAME = PlatoGuide
PlatoGuide_FILES = PlatoGuide.mm
PlatoGuide_CCFLAGS = -std=c++20 -fobjc-arc
PlatoGuide_FRAMEWORKS = Foundation JavaScriptCore
include $(THEOS_MAKE_PATH)/library.mk
