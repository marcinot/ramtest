#include <stdio.h>
#include <stdint.h>
#include <chrono> 
#include <pthread.h>
#include <errno.h>



using namespace std; 
using namespace std::chrono; 

const int TABLE_SIZE_MIN_BITS = 8;
const int TABLE_SIZE_MAX_BITS = 30;
const uint64_t TABLE_SIZE_MAX = 1UL << TABLE_SIZE_MAX_BITS;
const uint64_t NUM_READS = 50000000UL;

struct xorshift32_state {
  uint32_t a;
};

struct worker_arg {
	int tid;
	uint8_t* table;
	uint64_t table_size_bits;
	uint64_t num_reads;
	uint32_t seed;
	
	uint64_t output_sum;
};

/* The state word must be initialized to non-zero */
uint32_t xorshift32(struct xorshift32_state *state)
{
	/* Algorithm "xor" from p. 4 of Marsaglia, "Xorshift RNGs" */
	uint32_t x = state->a;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return state->a = x;
};




uint8_t* create_table(uint64_t table_size)
{
	xorshift32_state rnd;
	rnd.a = time(0);
	uint8_t* table = new uint8_t[table_size];		
	for(uint64_t i=0; i<table_size; i++)
	{
		table[i] = xorshift32(&rnd) & 0xff;
	}
	return table;
}

uint64_t benchmark_ram_randomread(uint8_t* table, uint64_t table_size_bits, uint64_t num_reads, uint32_t seed)
{
	uint64_t table_size_mask = (1 << table_size_bits) - 1;
	uint64_t sum = 0;
	xorshift32_state rnd;
	rnd.a = seed;
	
	for(uint64_t i=0; i<num_reads; i++)
	{
		uint64_t pos = (uint64_t(xorshift32(&rnd)) & table_size_mask);
		uint8_t v = table[pos];
		sum+=v;
	}
		
	return sum;
}


void* benchmark_ram_randomread_worker(void* arg) {	
	worker_arg* wa = (worker_arg*)arg;	
	wa->output_sum = benchmark_ram_randomread(wa->table, wa->table_size_bits, wa->num_reads, wa->seed);	
	return NULL;
}


uint64_t benchmark_ram_randomread_multithread(uint8_t* table, uint64_t table_size_bits, uint64_t num_reads, int num_threads)
{
	pthread_t* pids = new pthread_t[num_threads];
	worker_arg* args = new worker_arg[num_threads];
	
	for (int i=0; i < num_threads; i++) {
		args[i].tid = i;
		args[i].table = table;
		args[i].table_size_bits = table_size_bits;
		args[i].num_reads = num_reads / num_threads;		
		args[i].seed = (table_size_bits * (i+1) * num_threads) ^ (table_size_bits + (i+1) + num_threads);
		
		pthread_create(&pids[i], NULL, benchmark_ram_randomread_worker, &args[i]);
	}

	/* oczekiwanie na zakończenie wszystkich wątków */
	for (int i=0; i < num_threads; i++) {
		pthread_join(pids[i], NULL);
	}
	
	uint64_t sum = 0;
	for(int i=0; i<num_threads; i++)
	{
		sum += args[i].output_sum;
	}
		
	
	delete [] pids;
	return sum;
}


int main()
{	
	uint8_t* table = create_table(TABLE_SIZE_MAX);	
	
	for(int t=1; t<=32; t++)
	{		
		for(int table_size_bits = TABLE_SIZE_MIN_BITS; table_size_bits <= TABLE_SIZE_MAX_BITS; table_size_bits++)
		{
			uint64_t table_size = 1UL << table_size_bits;
			
		
	    		auto start = high_resolution_clock::now(); 
	   

			uint64_t sum = benchmark_ram_randomread_multithread(table, table_size_bits, NUM_READS, t);

			auto stop = high_resolution_clock::now(); 
			auto duration = duration_cast<microseconds>(stop - start); 
			double total_time = duration.count() / 1000000.0;
			
			printf("table_size=%lu num_threads=%d checksum=%lu time=%f reads_per_sec=%f\n", table_size, t, sum, total_time, NUM_READS / total_time );
				
		}
	}
	
	delete  table;	
	return 0;
}