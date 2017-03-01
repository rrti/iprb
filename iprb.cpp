#include <cstdio>
#include <cstring>

#include "iprb.hpp"

int main(int argc, char** argv) {
	if (argc < 3) {
		std::printf("[%s] usage: %s <mode='w'|'r'> <arg=\"...\">\n", __func__, argv[0]);
		return 0;
	}

	uint8_t buf[1024];
	uint8_t* arg = &buf[0];

	// use process name as buffer identifier
	ipc::t_spsc_ring_buffer<uint8_t> shm(sizeof(buf), sizeof(buf[0]), argv[0], argv[1]);

	switch (argv[1][0]) {
		case 'w': {
			std::printf("[prod::%s][arg=\"%s\"]\n", __func__, arg = reinterpret_cast<uint8_t*>(argv[2]));

			// producer process; copy argument to shared buffer
			for (size_t i = 0; i < shm.get_num_slots(); i++) {
				auto bws = std::move(shm.get_bw_slot());
				uint8_t* ptr = bws.get_ptr();

				*ptr = *arg;
				arg += ((*arg) != '\0');
			}
		} break;
		case 'r': {
			std::memset(&buf[0], 0, sizeof(buf));

			// consumer process; read argument from shared buffer
			for (size_t i = 0; i < shm.get_num_slots(); i++) {
				const auto brs = std::move(shm.get_br_slot());
				const uint8_t* ptr = brs.get_ptr();

				*arg = *ptr;
				arg += 1;
			}

			std::printf("[cons::%s][buf=\"%s\"]\n", __func__, buf);
		} break;
		default: {
		} break;
	}

	return 0;
}

