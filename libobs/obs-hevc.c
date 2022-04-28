/******************************************************************************
    Copyright (C) 2022 by Hugh Bailey <obs.jim@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "obs-hevc.h"

#include "obs-nal.h"
#include "util/darray.h"

enum {
	OBS_HEVC_NAL_TRAIL_N = 0,
	OBS_HEVC_NAL_TRAIL_R = 1,
	OBS_HEVC_NAL_TSA_N = 2,
	OBS_HEVC_NAL_TSA_R = 3,
	OBS_HEVC_NAL_STSA_N = 4,
	OBS_HEVC_NAL_STSA_R = 5,
	OBS_HEVC_NAL_RADL_N = 6,
	OBS_HEVC_NAL_RADL_R = 7,
	OBS_HEVC_NAL_RASL_N = 8,
	OBS_HEVC_NAL_RASL_R = 9,
	OBS_HEVC_NAL_VCL_N10 = 10,
	OBS_HEVC_NAL_VCL_R11 = 11,
	OBS_HEVC_NAL_VCL_N12 = 12,
	OBS_HEVC_NAL_VCL_R13 = 13,
	OBS_HEVC_NAL_VCL_N14 = 14,
	OBS_HEVC_NAL_VCL_R15 = 15,
	OBS_HEVC_NAL_BLA_W_LP = 16,
	OBS_HEVC_NAL_BLA_W_RADL = 17,
	OBS_HEVC_NAL_BLA_N_LP = 18,
	OBS_HEVC_NAL_IDR_W_RADL = 19,
	OBS_HEVC_NAL_IDR_N_LP = 20,
	OBS_HEVC_NAL_CRA_NUT = 21,
	OBS_HEVC_NAL_RSV_IRAP_VCL22 = 22,
	OBS_HEVC_NAL_RSV_IRAP_VCL23 = 23,
	OBS_HEVC_NAL_RSV_VCL24 = 24,
	OBS_HEVC_NAL_RSV_VCL25 = 25,
	OBS_HEVC_NAL_RSV_VCL26 = 26,
	OBS_HEVC_NAL_RSV_VCL27 = 27,
	OBS_HEVC_NAL_RSV_VCL28 = 28,
	OBS_HEVC_NAL_RSV_VCL29 = 29,
	OBS_HEVC_NAL_RSV_VCL30 = 30,
	OBS_HEVC_NAL_RSV_VCL31 = 31,
	OBS_HEVC_NAL_VPS = 32,
	OBS_HEVC_NAL_SPS = 33,
	OBS_HEVC_NAL_PPS = 34,
	OBS_HEVC_NAL_AUD = 35,
	OBS_HEVC_NAL_EOS_NUT = 36,
	OBS_HEVC_NAL_EOB_NUT = 37,
	OBS_HEVC_NAL_FD_NUT = 38,
	OBS_HEVC_NAL_SEI_PREFIX = 39,
	OBS_HEVC_NAL_SEI_SUFFIX = 40,
};

bool obs_hevc_keyframe(const uint8_t *data, size_t size)
{
	const uint8_t *nal_start, *nal_end;
	const uint8_t *end = data + size;

	nal_start = obs_nal_find_startcode(data, end);
	while (true) {
		while (nal_start < end && !*(nal_start++))
			;

		if (nal_start == end)
			break;

		const uint8_t type = (nal_start[0] & 0x7F) >> 1;

		if (type <= OBS_HEVC_NAL_RSV_IRAP_VCL23)
			return type >= OBS_HEVC_NAL_BLA_W_LP;

		nal_end = obs_nal_find_startcode(nal_start, end);
		nal_start = nal_end;
	}

	return false;
}

void obs_extract_hevc_headers(const uint8_t *packet, size_t size,
			      uint8_t **new_packet_data,
			      size_t *new_packet_size, uint8_t **header_data,
			      size_t *header_size, uint8_t **sei_data,
			      size_t *sei_size)
{
	DARRAY(uint8_t) new_packet;
	DARRAY(uint8_t) header;
	DARRAY(uint8_t) sei;
	const uint8_t *nal_start, *nal_end, *nal_codestart;
	const uint8_t *end = packet + size;

	da_init(new_packet);
	da_init(header);
	da_init(sei);

	nal_start = obs_nal_find_startcode(packet, end);
	nal_end = NULL;
	while (nal_end != end) {
		nal_codestart = nal_start;

		while (nal_start < end && !*(nal_start++))
			;

		if (nal_start == end)
			break;

		const uint8_t type = (nal_start[0] & 0x7F) >> 1;

		nal_end = obs_nal_find_startcode(nal_start, end);
		if (!nal_end)
			nal_end = end;

		if (type == OBS_HEVC_NAL_VPS || type == OBS_HEVC_NAL_SPS ||
		    type == OBS_HEVC_NAL_PPS) {
			da_push_back_array(header, nal_codestart,
					   nal_end - nal_codestart);
		} else if (type == OBS_HEVC_NAL_SEI_PREFIX ||
			   type == OBS_HEVC_NAL_SEI_SUFFIX) {
			da_push_back_array(sei, nal_codestart,
					   nal_end - nal_codestart);

		} else {
			da_push_back_array(new_packet, nal_codestart,
					   nal_end - nal_codestart);
		}

		nal_start = nal_end;
	}

	*new_packet_data = new_packet.array;
	*new_packet_size = new_packet.num;
	*header_data = header.array;
	*header_size = header.num;
	*sei_data = sei.array;
	*sei_size = sei.num;
}
