TARGET      := MyPackUpdater
APP_TITLE   := MyPack Auto Updater
APP_AUTHOR  := Draco
APP_VERSION := 1.0.1
APP_ICON := $(CURDIR)/icon.jpg
ifeq ($(strip $(DEVKITPRO)),)
    $(error "Please set DEVKITPRO in your environment. export DEVKITPRO=/c/devkitPro")
endif

include $(DEVKITPRO)/libnx/switch_rules

BUILD       := build
SOURCES     := source
INCLUDES    := include

# Cấu hình thư viện
LIBS    := -lcurl -lminizip -lz -lnx -lm
LIBDIRS := $(PORTLIBS) $(LIBNX)

# Flags
ARCH    := -march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS  := -g -Wall -O2 -ffunction-sections $(ARCH) -I$(LIBNX)/include -I$(PORTLIBS)/include -D__SWITCH__
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions
LDFLAGS  := -specs=$(LIBNX)/switch.specs -g $(ARCH) -L$(LIBNX)/lib $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)
export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR  := $(CURDIR)/$(BUILD)
CFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES    := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
export OFILES   := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o)
export INCLUDE  := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) -I$(CURDIR)/$(BUILD)

.PHONY: $(BUILD) clean all

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).elf

else

$(OUTPUT).nro   : $(OUTPUT).elf $(OUTPUT).nacp
	@echo Creating NRO...
	@elf2nro $< $@ --icon=$(TOPDIR)/icon.jpg --nacp=$(OUTPUT).nacp

$(OUTPUT).nacp  :
	@echo Creating NACP Metadata...
	@nacptool --create "$(APP_TITLE)" "$(APP_AUTHOR)" "$(APP_VERSION)" $@

$(OUTPUT).elf   : $(OFILES)
	@echo Linking...
	@$(CXX) $(LDFLAGS) $(OFILES) $(LIBS) -o $@

endif