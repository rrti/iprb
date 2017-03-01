#ifndef INTERPROCESS_RINGBUFFER_HDR
#define INTERPROCESS_RINGBUFFER_HDR

#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/sync/interprocess_semaphore.hpp>
#include <boost/interprocess/allocators/allocator.hpp>

namespace ipc {
	// shared between one producer and one consumer process
	// when empty the producer must put bytes in before the
	// consumer can take any bytes out, while when full the
	// consumer must take bytes out before consumer can put
	// any bytes in
	template<typename t_data>
	class t_spsc_ring_buffer {
	public:
		typedef  typename boost::interprocess::managed_shared_memory::segment_manager  t_seg_manager;
		typedef  typename boost::interprocess::allocator<t_data, t_seg_manager>  t_seg_allocator;

		template<bool block, bool write> struct t_buffer_slot {
		public:
			t_buffer_slot(t_spsc_ring_buffer<t_data>* buf): m_buf(buf) {
				switch (write) {
					case  true: { set_ptr(m_buf->try_get_prod_ptr(m_idx, !block)); } break;
					case false: { set_ptr(m_buf->try_get_cons_ptr(m_idx, !block)); } break;
				}
			}

			t_buffer_slot(t_buffer_slot&& slot) { *this = std::move(slot); }

			~t_buffer_slot() {
				if (m_ptr == nullptr)
					return;

				switch (write) {
					case  true: { m_buf->open_cons_slot(); } break;
					case false: { m_buf->open_prod_slot(); } break;
				}
			}

			t_buffer_slot& operator = (t_buffer_slot&& slot) {
				set_buf(slot.get_buf());
				set_ptr(slot.get_ptr());
				set_idx(slot.get_idx());
				slot.set_buf(nullptr);
				slot.set_ptr(nullptr);
				return *this;
			}

			const t_spsc_ring_buffer<t_data>* get_buf() const { return m_buf; }
			      t_spsc_ring_buffer<t_data>* get_buf()       { return m_buf; }

			const t_data* get_ptr() const { return m_ptr; }
			      t_data* get_ptr()       { return m_ptr; }

			size_t get_idx() const { return m_idx; }

		private:
			t_buffer_slot(const t_buffer_slot& src) = delete;
			t_buffer_slot& operator = (const t_buffer_slot& src) = delete;

			void set_buf(t_spsc_ring_buffer<t_data>* buf) { m_buf = buf; }
			void set_ptr(t_data* ptr) { m_ptr = ptr; }
			void set_idx(size_t idx) { m_idx = idx; }

		private:
			t_spsc_ring_buffer<t_data>* m_buf;

			t_data* m_ptr;
			size_t m_idx;
		};



		t_spsc_ring_buffer(size_t num_slots, size_t slot_size, const char* name, const char* mode)
			: m_prod_sem(num_slots)
			, m_cons_sem(0)
			, m_dtor_sem(0)

			#if 0
			, m_seg_manager(num_slots * slot_size)
			, m_seg_allocator(&m_seg_manager)
			#endif

			, m_cons_idx(0)
			, m_prod_idx(0)
			, m_num_slots(num_slots)
			, m_slot_size(slot_size)

			, m_name(name)
			, m_mode(mode)
		{
			#if 0
			m_shared_mem_ptr = m_seg_allocator.allocate(m_num_slots * m_slot_size);
			#else
			// create the buffer and pre-allocate its entire capacity;
			// also get the offset from the start of the shared region
			// which is mapped to a unique virtual address per process
			switch (m_mode[0]) {
				case 'w': {
					m_shared_mem_buf = boost::interprocess::managed_shared_memory(boost::interprocess::create_only, m_name, m_num_slots * m_slot_size);
					m_shared_mem_ptr = reinterpret_cast<t_data*>(m_shared_mem_buf.allocate(m_num_slots * m_slot_size));
				} break;
				case 'r': {
					m_shared_mem_buf = boost::interprocess::managed_shared_memory(boost::interprocess::open_only, m_name);
					m_shared_mem_ptr = reinterpret_cast<t_data*>(m_shared_mem_buf.get_address());
				} break;
				default: {
				} break;
			}

			assert(m_shared_mem_ptr.get() != nullptr);
			assert(m_shared_mem_buf.belongs_to_segment(m_shared_mem_ptr.get()));
			#endif
		}

		~t_spsc_ring_buffer() {
			if (m_mode[0] != 'w') {
				m_dtor_sem.post();
				return;
			}

			// wait for consumer to destruct first, so the buffer is unmapped
			// and producer can remove it (~managed_shared_memory only unmaps)
			m_dtor_sem.wait();

			#if 0
			m_seg_allocator.deallocate(m_shared_mem_ptr.get(), m_num_slots * m_slot_size);
			#else
			m_shared_mem_buf.deallocate(m_shared_mem_ptr.get());
			#endif

			if (boost::interprocess::shared_memory_object::remove(m_name))
				return;

			throw (std::runtime_error("[~buffer] memory still mapped by consumer process"));
		}

		t_buffer_slot< true,  true> get_bw_slot() { return (t_buffer_slot< true,  true>(this)); }
		t_buffer_slot< true, false> get_br_slot() { return (t_buffer_slot< true, false>(this)); }
		t_buffer_slot<false,  true> get_aw_slot() { return (t_buffer_slot<false,  true>(this)); }
		t_buffer_slot<false, false> get_ar_slot() { return (t_buffer_slot<false, false>(this)); }

		size_t get_num_slots() const { return m_num_slots; }
		size_t get_slot_size() const { return m_slot_size; }
		
	private:
		t_data* try_get_prod_ptr(size_t& slot_idx, bool async) {
			if (async && !m_prod_sem.try_wait())
				return nullptr;
			return (get_prod_ptr(slot_idx, !async));
		}

		t_data* get_prod_ptr(size_t& slot_idx, bool block) {
			if (block)
				m_prod_sem.wait();

			// pass back the current index
			slot_idx = m_prod_idx % m_num_slots;
			m_prod_idx += 1;

			return &m_shared_mem_ptr[slot_idx * m_slot_size];
		}

		t_data* try_get_cons_ptr(size_t& slot_idx, bool async) {
			if (async && !m_cons_sem.try_wait())
				return nullptr;
			return (get_cons_ptr(slot_idx, !async));
		}
		t_data* get_cons_ptr(size_t& slot_idx, bool block) {
			if (block)
				m_cons_sem.wait();

			slot_idx = m_cons_idx % m_num_slots;
			m_cons_idx += 1;

			return &m_shared_mem_ptr[slot_idx * m_slot_size];
		}

		void open_cons_slot() { m_cons_sem.post(); }
		void open_prod_slot() { m_prod_sem.post(); }

		bool empty() const { return (m_cons_idx == m_prod_idx); }
		bool full() const { return ((m_prod_idx - m_cons_idx) == m_num_slots); }

	private:
		boost::interprocess::interprocess_semaphore m_prod_sem;
		boost::interprocess::interprocess_semaphore m_cons_sem;
		boost::interprocess::interprocess_semaphore m_dtor_sem;

		#if 0
		boost::interprocess::managed_shared_memory::segment_manager m_seg_manager;
		boost::interprocess::allocator<t_data, t_seg_manager> m_seg_allocator;
		#else
		boost::interprocess::managed_shared_memory m_shared_mem_buf;
		#endif
		boost::interprocess::offset_ptr<t_data> m_shared_mem_ptr;

		size_t m_cons_idx; // consumer index (tail; read)
		size_t m_prod_idx; // producer index (head; write)
		size_t m_num_slots;
		size_t m_slot_size;

		const char* m_name;
		const char* m_mode;
	};
};

#endif

