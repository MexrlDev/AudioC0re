NAME    := audioC0re
CC      := gcc
OBJCOPY := objcopy
PYTHON  ?= python3
SRC_DIR := src
BUILD   := build

CFLAGS := -m64 -ffreestanding -nostdinc -fPIE \
          -fno-stack-protector -fno-builtin \
          -fno-asynchronous-unwind-tables -fno-unwind-tables \
          -mno-red-zone -fno-omit-frame-pointer -mgeneral-regs-only \
          -Os -Wall -Wextra -Wno-unused-parameter -Wno-unused-function \
          -Wno-missing-field-initializers \
          -I$(SRC_DIR)

LDFLAGS := -nostdlib -nostartfiles -nodefaultlibs -pie \
           -Wl,-T,$(CURDIR)/linker.ld -Wl,--build-id=none

SRCS_C := $(wildcard $(SRC_DIR)/*.c)
OBJS   := $(patsubst $(SRC_DIR)/%.c,$(BUILD)/%.o,$(SRCS_C))

all: $(NAME).elf $(NAME).bin

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/%.o: $(SRC_DIR)/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(NAME).elf: $(OBJS) linker.ld
	$(CC) $(LDFLAGS) $(OBJS) -o $@

$(NAME).bin: $(NAME).elf
	$(OBJCOPY) -O binary $< $@

hex: $(NAME).bin
	xxd -p $(NAME).bin | tr -d '\n' > $(NAME).hex

clean:
	rm -rf $(BUILD) $(NAME).elf $(NAME).bin $(NAME).hex

.PHONY: all hex clean
