#!/bin/sh

EVT_CTL="./install/bin/evt_ctl"

$EVT_CTL -C o:4				\
	-a "20-24@2:black"		\
	-a "90-95@4:coffee"		\
	-a "50-56@2:scarlet"		\
	-a "57-61@2:witch"		\
	-a "96-99@2:blue"		\
	-a "78-82@4:hello"		\
	-a "25-29@3:widow"		\
	-a "10-15@2:spider"		\
	-a "35-40@4:yellow"		\
	-a "0-3@1:bulk"			\
	-f "20-30@3"			\
	-f "28-52@3"			\
	-f "20-30@0"			\
	-f "0-100@1"			\
	-f "0-100@4"			\
	-b "-1"				\
	-D
