# SPDX-License-Identifier: BSD-3-Clause
P := runtime
D := deps
B := build/host
CPPFLAGS := -I$(P) -I$(D)/BareMSX/compat -I$(D)/devices -I$(D)/Z80/API -I$(D)/Zeta/API -I$(D)/emu2149 -I$(D)/emu2212 -DZ80_STATIC -DMSX1_TEST
CFLAGS := -std=gnu11 -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -Wall -Wextra -Wno-unused-parameter -MMD -MP
LDFLAGS := -fsanitize=address,undefined -no-pie
OBJS := $(addprefix $(B)/,msx1_runtime.o msx1_vdp.o mapper.o Z80.o msx1_psg.o emu2212.o)
all: $(B)/msx1
$(B):
	mkdir -p $@
$(B)/msx1: $(OBJS) $(B)/host_test.o
	$(CC) $(LDFLAGS) $^ -lm -o $@
$(B)/%.o: $(P)/%.c | $(B)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(B)/host_test.o: tests/host_test.c | $(B)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(B)/%.o: $(D)/devices/%.c | $(B)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(B)/Z80.o: $(D)/Z80/sources/Z80.c | $(B)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(B)/emu2149.o: $(D)/emu2149/emu2149.c | $(B)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(B)/emu2212.o: $(D)/emu2212/emu2212.c | $(B)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
-include $(wildcard $(B)/*.d)
