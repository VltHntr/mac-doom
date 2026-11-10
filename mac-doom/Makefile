################################################################
#
# $Id:$
#
# $Log:$
#
CC=  gcc  # gcc or g++

CFLAGS=-g -Wall -DNORMALUNIX -DLINUX # -DUSEASM 
LDFLAGS=-L/usr/X11R6/lib
LIBS=-lXext -lX11 -lnsl -lm

# subdirectory for objects
O=linux

# Native macOS target. AppKit presents the existing CPU-rendered framebuffer;
# audio remains a silent stub. Neither X11 nor Linux OSS is linked.
MACOS_CC ?= clang
MACOS_BUILD_DIR ?= build/macos
MACOS_OBJ_DIR ?= $(MACOS_BUILD_DIR)/obj
MACOS_CFLAGS ?= -g -Wall -std=gnu11 -DNORMALUNIX -I.
MACOS_OBJCFLAGS ?= -g -Wall -I. -fobjc-arc
MACOS_LIBS ?= -lm -framework AppKit -framework ApplicationServices -framework AudioToolbox
MACOS_SRCS = $(filter-out i_video.c i_sound.c,$(wildcard *.c)) macos/i_sound_macos.c
MACOS_OBJC_SRCS = macos/i_video_macos.m
MACOS_OBJS = $(patsubst %.c,$(MACOS_OBJ_DIR)/%.o,$(MACOS_SRCS)) \
	 $(patsubst %.m,$(MACOS_OBJ_DIR)/%.o,$(MACOS_OBJC_SRCS))
MACOS_WAD = $(MACOS_BUILD_DIR)/doom1.wad

# not too sophisticated dependency
OBJS=				\
		$(O)/doomdef.o		\
		$(O)/doomstat.o		\
		$(O)/dstrings.o		\
		$(O)/i_system.o		\
		$(O)/i_sound.o		\
		$(O)/i_video.o		\
		$(O)/i_net.o			\
		$(O)/tables.o			\
		$(O)/f_finale.o		\
		$(O)/f_wipe.o 		\
		$(O)/d_main.o			\
		$(O)/d_net.o			\
		$(O)/d_items.o		\
		$(O)/g_game.o			\
		$(O)/m_menu.o			\
		$(O)/m_misc.o			\
		$(O)/m_argv.o  		\
		$(O)/m_bbox.o			\
		$(O)/m_fixed.o		\
		$(O)/m_swap.o			\
		$(O)/m_cheat.o		\
		$(O)/m_random.o		\
		$(O)/am_map.o			\
		$(O)/p_ceilng.o		\
		$(O)/p_doors.o		\
		$(O)/p_enemy.o		\
		$(O)/p_floor.o		\
		$(O)/p_inter.o		\
		$(O)/p_lights.o		\
		$(O)/p_map.o			\
		$(O)/p_maputl.o		\
		$(O)/p_plats.o		\
		$(O)/p_pspr.o			\
		$(O)/p_setup.o		\
		$(O)/p_sight.o		\
		$(O)/p_spec.o			\
		$(O)/p_switch.o		\
		$(O)/p_mobj.o			\
		$(O)/p_telept.o		\
		$(O)/p_tick.o			\
		$(O)/p_saveg.o		\
		$(O)/p_user.o			\
		$(O)/r_bsp.o			\
		$(O)/r_data.o			\
		$(O)/r_draw.o			\
		$(O)/r_main.o			\
		$(O)/r_plane.o		\
		$(O)/r_segs.o			\
		$(O)/r_sky.o			\
		$(O)/r_things.o		\
		$(O)/w_wad.o			\
		$(O)/wi_stuff.o		\
		$(O)/v_video.o		\
		$(O)/st_lib.o			\
		$(O)/st_stuff.o		\
		$(O)/hu_stuff.o		\
		$(O)/hu_lib.o			\
		$(O)/s_sound.o		\
		$(O)/z_zone.o			\
		$(O)/info.o				\
		$(O)/sounds.o

all:	 $(O)/linuxxdoom

macos: $(MACOS_BUILD_DIR)/doom $(MACOS_WAD)

clean:
	rm -f *.o *~ *.flc
	rm -f linux/*

clean-macos:
	rm -rf $(MACOS_BUILD_DIR)

$(O)/linuxxdoom:	$(OBJS) $(O)/i_main.o
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) $(O)/i_main.o \
	-o $(O)/linuxxdoom $(LIBS)

$(O)/%.o:	%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(MACOS_BUILD_DIR)/doom: $(MACOS_OBJS)
	$(MACOS_CC) $(MACOS_CFLAGS) $(MACOS_OBJS) -o $@ $(MACOS_LIBS)

$(MACOS_OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(MACOS_CC) $(MACOS_CFLAGS) -c $< -o $@

$(MACOS_OBJ_DIR)/%.o: %.m
	@mkdir -p $(dir $@)
	$(MACOS_CC) $(MACOS_OBJCFLAGS) -c $< -o $@

$(MACOS_WAD): ../wad/doom.wad
	@mkdir -p $(dir $@)
	cp $< $@

.PHONY: macos clean-macos

#############################################################
#
#############################################################
