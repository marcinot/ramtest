#include <stdio.h>
#include <stdint.h>
#include <chrono> 
#include <pthread.h>
#include <errno.h>



using namespace std; 
using namespace std::chrono; 

typedef unsigned __int128 word_type;

const uint64_t TABLE_SIZE = 268435456UL;
const uint64_t BATCH_BUFFER_SIZE = 8*1024*1024;



struct xorshift32_state {
  uint32_t a;
};

struct worker_arg {
	int tid;
	word_type* table;
	uint64_t batch_buffer_size;
	uint64_t* batch_buffer_pos;
	word_type* batch_buffer_words;
	int num_threads;	
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




word_type* create_table(uint64_t table_size)
{
	xorshift32_state rnd;
	rnd.a = time(0);
	word_type* table = new word_type[table_size];		
	for(uint64_t i=0; i<table_size; i++)
	{
		table[i] = xorshift32(&rnd) & 0xff;
	}
	return table;
}


void* benchmark_ram_randomread_worker(void* arg) {	
	

	worker_arg* wa = (worker_arg*)arg;	
	
	uint64_t worker_batch_size = wa->batch_buffer_size / wa->num_threads;
	
	uint64_t start_idx = worker_batch_size * wa->tid;
	uint64_t end_idx = start_idx + worker_batch_size;
	
	
	
	for(uint64_t idx = start_idx; idx<end_idx; idx++)
	{
		wa->batch_buffer_words[idx] = wa->table [ wa->batch_buffer_pos[idx] ]; 
	}
	

				
	return NULL;
}

void gen_batch_buffer_pos(uint64_t* batch_buffer_pos, uint64_t batch_buffer_size)
{
	xorshift32_state rnd;
	rnd.a = time(0);
	
	for(uint64_t i=0; i<batch_buffer_size; i++)
		batch_buffer_pos[i] = xorshift32(&rnd) % TABLE_SIZE;		
		
}



uint64_t benchmark_ram_randomread_multithread(word_type* table, uint64_t batch_buffer_size, int num_threads, double& total_time)
{
	uint64_t* batch_buffer_pos = new uint64_t[batch_buffer_size];
	word_type* batch_buffer_words = new word_type[batch_buffer_size];
	
	gen_batch_buffer_pos(batch_buffer_pos, batch_buffer_size);

	pthread_t* pids = new pthread_t[num_threads];
	worker_arg* args = new worker_arg[num_threads];
	
		auto start = high_resolution_clock::now(); 

	for (int i=0; i < num_threads; i++) {
		args[i].tid = i;
		args[i].table = table;
		args[i].batch_buffer_pos = batch_buffer_pos;
		args[i].batch_buffer_words = batch_buffer_words;
		args[i].num_threads = num_threads;
		args[i].batch_buffer_size = batch_buffer_size;
						
		pthread_create(&pids[i], NULL, benchmark_ram_randomread_worker, &args[i]);
	}


	/* oczekiwanie na zakończenie wszystkich wątków */
	for (int i=0; i < num_threads; i++) {
		pthread_join(pids[i], NULL);		
	}
	

	auto stop = high_resolution_clock::now(); 
	auto duration = duration_cast<microseconds>(stop - start); 
	total_time = duration.count() / 1000000.0;	

	
	uint64_t sum = 0;
	for(int i=0; i<batch_buffer_size; i++)
	{
		sum += batch_buffer_words[i];
	}
		
	delete [] batch_buffer_pos;
	delete [] batch_buffer_words;
	 	
	delete [] pids;
	return sum;
}


int main()
{	
	word_type* table = create_table(TABLE_SIZE);	

	int t = 1;
	while(t <= 128)
	{							
		auto start = high_resolution_clock::now(); 
		double workers_total_time = 0.0;
		
		uint64_t sum = benchmark_ram_randomread_multithread(table, BATCH_BUFFER_SIZE, t, workers_total_time);
		auto stop = high_resolution_clock::now(); 
		auto duration = duration_cast<microseconds>(stop - start); 
		double total_time = duration.count() / 1000000.0;
		
		printf("table_size=%lu num_threads=%d checksum=%lu total_time=%f workers_time=%f copy_per_sec=%f\n", TABLE_SIZE, t, sum, total_time, workers_total_time, BATCH_BUFFER_SIZE / workers_total_time );		
		
		t = t * 2;
	}
	
	delete [] table;	
	return 0;
}
