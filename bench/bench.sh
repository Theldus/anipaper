#!/usr/bin/env bash

# MIT License
#
# Copyright (c) 2021 Davidson Francis <davidsondfgl@gmail.com>
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

# Paths
CURDIR="$( cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
cd "${CURDIR}/"

# Colors
GREEN="\033[1;32m"
YELLOW="\033[1;33m"
NC="\033[0m"

function check_and_convert()
{
	file="$1"
	width="$2"
	height="$3"
	fps="$4"
	step="$5"

	if [ -f "lake${height}p_${fps}.mp4" ]; then
		printf "Skipping lake${height}p_${fps}.mp4\n"
	else
		printf "${GREEN}(${step}/5) Converting to ${height}p, ${fps} fps...${NC} "
		ffmpeg -i "${file}" -r ${fps} -vf scale=$width:$height \
			"lake${height}p_${fps}.mp4" &> /dev/null
		printf "done\n"
	fi	
}

function run()
{
	input_file="lake${1}p_${2}.mp4"
	text_file="lake${1}p_${2}.txt"
	step="${3}"
	
	printf "${YELLOW}Input file: ${input_file} (${step}/6)\n"
	printf "NO HW ACCEL:\n" > "${text_file}"
	printf "  ${GREEN}Run #1/6 hw accel: no${NC}\n"
	sudo intel_gpu_time anipaper "${input_file}" -o >> "${text_file}"
	printf "  ${GREEN}Run #2/6 hw accel: no${NC}\n"
	sudo intel_gpu_time anipaper "${input_file}" -o >> "${text_file}"
	printf "  ${GREEN}Run #3/6 hw accel: no${NC}\n"
	sudo intel_gpu_time anipaper "${input_file}" -o >> "${text_file}"

	printf "HW ACCEL:\n" >> "${text_file}"
	printf "  ${GREEN}Run #4/6 hw accel: yes${NC}\n"
	sudo intel_gpu_time anipaper "${input_file}" -o -d vaapi >> "${text_file}"
	printf "  ${GREEN}Run #5/6 hw accel: yes${NC}\n"
	sudo intel_gpu_time anipaper "${input_file}" -o -d vaapi >> "${text_file}"
	printf "  ${GREEN}Run #6/6 hw accel: yes${NC}\n"
	sudo intel_gpu_time anipaper "${input_file}" -o -d vaapi >> "${text_file}"
}

if [ ! -x "$(command -v ffmpeg)" ]; then
	printf "FFmpeg not found in PATH!!\n"
	exit 1
fi

if [ ! -x "$(command -v anipaper)" ]; then
	printf "Anipaper not found in PATH!!\n"
	exit 1
fi

if [ ! -x "$(command -v intel_gpu_time)" ]; then
	printf "intel_gpu_time not found!!\n"
	exit 1
fi

printf "${YELLOW}[+] Checking if intel_gpu_time works...${NC} "
sudo intel_gpu_time ls &> /dev/null
if [ $? -eq 1 ]; then
	printf "no\n"
	printf "  > Please check if you have a proper Intel graphics card up and\n"
	printf "  > running. Aborting\n"
	exit 1
else
	printf "yes\n"
fi
	

printf "${YELLOW}[+] Converting videos (this may take a while)...${NC}\n"

check_and_convert "lake1440p_60.mp4" 2560 1440 30 1 # 1440p 30 fps
check_and_convert "lake1440p_60.mp4" 1920 1080 60 2 # 1080p 60 fps
check_and_convert "lake1080p_60.mp4" 1920 1080 30 3 # 1080p 30 fps
check_and_convert "lake1080p_60.mp4" 1280  720 60 4 #  720p 60 fps
check_and_convert "lake720p_60.mp4"  1280  720 30 5 #  720p 30 fps

printf "${YELLOW}[+] Running Anipaper${NC}\n"
run 1440 60 1
run 1440 30 2
run 1080 60 3
run 1080 30 4
run  720 60 5
run  720 30 6
