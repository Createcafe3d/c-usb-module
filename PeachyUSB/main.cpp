#include <stdio.h>
#include <stdlib.h>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <libusb.h>
#include <cmath>

typedef struct {
	unsigned char data[64];
	uint32_t length;
} packet_t;

class PeachyUsb;


class PeachyUsb {
private:
	std::condition_variable write_avail;
	std::condition_variable read_avail;
	std::mutex mtx;
	std::thread usb_writer;

	bool run_writer;
	uint32_t write_r_index; // index to read from
	uint32_t write_count; // number of packets ready to read (written and unread)
	uint32_t write_capacity; // total capacity
	packet_t* write_packets;

	libusb_context* usb_context;
	libusb_device_handle* usb_handle;

public:
	static void writer_func(PeachyUsb* ctx) {
		unsigned char buf[64] = { 0 };
		int packet_size;
		int transferred;

		while (ctx->run_writer) {
			ctx->get_from_write_queue(buf, 64, &packet_size);
			int res = libusb_bulk_transfer(ctx->usb_handle, 2, buf, packet_size, &transferred, 2000);
			if (res != 0) {
				printf("libusb error %d: %s\n", res, libusb_error_name(res));
			}
		}
		libusb_release_interface(ctx->usb_handle, 0);
		libusb_exit(ctx->usb_context);
		ctx->usb_handle = NULL;
		ctx->usb_context = NULL;
	}

	PeachyUsb(uint32_t buffer_size) {
		this->write_capacity = buffer_size;
		this->write_r_index = 0;
		this->write_count = 0;
		this->write_packets = (packet_t*)malloc(sizeof(packet_t) * buffer_size);
	}
	~PeachyUsb() {
		if (this->usb_context) {
			this->run_writer = false;
		}
		this->usb_writer.join();
	}
	int start() {
		libusb_init(&this->usb_context);
		this->usb_handle = libusb_open_device_with_vid_pid(this->usb_context, 0x16d0, 0x0af3);
		if (this->usb_handle == NULL) {
			return -1;
		}
		libusb_claim_interface(this->usb_handle, 0);

		this->run_writer = true;
		this->usb_writer = std::thread(writer_func, this);
	}

	void get_from_write_queue(unsigned char* buf, int length, int* transferred) {
		std::unique_lock<std::mutex> lck(mtx);
		while (this->write_count == 0) {
			read_avail.wait(lck);
		}
		packet_t* pkt = &this->write_packets[this->write_r_index];
		int read_count = (pkt->length > length ? length : pkt->length);
		memcpy(buf, pkt->data, read_count);
		*transferred = read_count;
		this->write_count--;
		this->write_r_index = (this->write_r_index + 1) % this->write_capacity;
		this->write_avail.notify_one();
	}

	void write(unsigned char* buf, int length) {
		if (!this->usb_handle) {
			return;
		}
		std::unique_lock<std::mutex> lck(mtx);
		while (this->write_count == this->write_capacity) {
			this->write_avail.wait(lck);
		}
		uint32_t write_w_index = (this->write_r_index + this->write_count) % this->write_capacity;
		this->write_packets[write_w_index].length = length;
		memcpy(this->write_packets[write_w_index].data, buf, length);
		this->write_count++;
		this->read_avail.notify_one();
	}

	void read(unsigned char* buf, int length, int* transferred) {
		*transferred = 0;
		if (!this->usb_handle) { 
			return; 
		}
		libusb_bulk_transfer(this->usb_handle, 0x83, buf, length, transferred, 2000);
	}
};

extern "C" {
	__declspec(dllexport) PeachyUsb *peachyusb_init(uint32_t capacity) {
		PeachyUsb* ctx = new PeachyUsb(capacity);
		ctx->start();
		return ctx;
	}

	__declspec(dllexport) int peachyusb_read(PeachyUsb* ctx, unsigned char* buf, uint32_t length) {
		int transferred;
		ctx->read(buf, length, &transferred);
		return transferred;
	}

	__declspec(dllexport) void peachyusb_write(PeachyUsb* ctx, unsigned char* buf, uint32_t length) {
		ctx->write(buf, length);
	}

	__declspec(dllexport) void peachyusb_shutdown(PeachyUsb* ctx) {
		delete ctx;
	}
}