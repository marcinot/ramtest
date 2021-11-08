#include <stdio.h>
#include <stdint.h>
#include <chrono> 
#include <pthread.h>
#include <errno.h>
#include <assert.h>
#include <emmintrin.h>
#include <smmintrin.h>
#include <errno.h>
#include "SafeQueue.h"

#define handle_error_en(en, msg) \
        do { errno = en; perror(msg); exit(EXIT_FAILURE); } while (0)

#define RANDDEV "/dev/urandom"

using namespace std; 
using namespace std::chrono; 

typedef unsigned __int128 word_type;
typedef uint64_t pos_type;

uint64_t TABLE_SIZE = 268435456UL;
uint64_t BATCH_BUFFER_SIZE = 16*1024*1024;




struct xorshift32_state {
  uint32_t a;
};

struct worker_arg {
	int tid;
};

struct batch_work {
	word_type* table;	
	pos_type* batch_buffer_pos;
	word_type* batch_buffer_words;
	uint64_t start;
	uint64_t end;
};

struct batch_result {
	uint64_t sum;
};

pthread_t* pids;
worker_arg* args;
SafeQueue<batch_work> works_queue;
SafeQueue<batch_result> results_queue;

void* hugealloc(size_t bytes)
{
        size_t  huge_page_size = 2048*1024;
        uint64_t nh = bytes / huge_page_size;
        if ((bytes % huge_page_size) != 0)
        {
                printf("Invalid param bytes for hugealloc %lu\n", bytes);
                exit(-1);
        }
        printf("Allocating %lu Huge pages\n", nh);
        void *ptr = mmap(NULL, nh * (1 << 21), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB,-1, 0);
	if (ptr == MAP_FAILED)
	{
		perror("hugealloc error");
		exit(-1);
	}
        printf("Allocated Huge pages\n");
        return ptr;
}

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





unsigned long long bigrand(void) {
    FILE *rdp;
    unsigned long long num;

    rdp = fopen(RANDDEV, "rb");
    assert(rdp);

    assert(fread(&num, sizeof(num), 1, rdp) == 1);

    fclose(rdp);

    return num;
}


word_type* create_table(uint64_t table_size)
{
	xorshift32_state rnd;
	rnd.a = bigrand();
	//word_type* table = new word_type[table_size];		
	word_type* table = (word_type*) hugealloc(table_size * sizeof(word_type));
	for(uint64_t i=0; i<table_size; i++)
	{
		//table[i] = xorshift32(&rnd) & 0xff;
		table[i] = i;
	}
	return table;
}

void affinity_set(int id)
{
           int s;
           cpu_set_t cpuset;
           pthread_t thread;

        thread = pthread_self();

           /* Set affinity mask to include CPUs 0 to 7. */

           CPU_ZERO(&cpuset);
           //for (int j = 0; j < 32; j++)
               CPU_SET(id, &cpuset);

           s = pthread_setaffinity_np(thread, sizeof(cpuset), &cpuset);
           if (s != 0)
               handle_error_en(s, "pthread_setaffinity_np");

           /* Check the actual affinity mask assigned to the thread. */

           s = pthread_getaffinity_np(thread, sizeof(cpuset), &cpuset);
           if (s != 0)
               handle_error_en(s, "pthread_getaffinity_np");

           //printf("[%d] Set returned by pthread_getaffinity_np() contained:\n", id);
           /*for (int j = 0; j < CPU_SETSIZE; j++)
               if (CPU_ISSET(j, &cpuset))
                   printf("[%d]    CPU %d\n", id, j);*/

}


void* benchmark_ram_randomread_worker(void* arg) {	
	
	worker_arg* wa = (worker_arg*)arg;	
	affinity_set(wa->tid);
	
	while(true) {
		batch_work work = works_queue.dequeue();
		if (work.end == 0)
			break;
		
	
		
		
		uint64_t start_idx = work.start;
		uint64_t end_idx = work.end;
		
		
		word_type* out_buff = work.batch_buffer_words;
		word_type* in_buff = work.table;
		pos_type* pos_buff = work.batch_buffer_pos;
		
		
		for(uint64_t idx = start_idx; idx<end_idx; idx++)
		{						
			pos_type p = pos_buff[idx];		
			const __m128i val = _mm_stream_load_si128 ((__m128i *)(in_buff + p) );
			_mm_stream_si128 ((__m128i *)(out_buff + idx), val);
		}
		
		batch_result res;
		results_queue.enqueue(res);
	}
	
					
	return NULL;
}

void gen_batch_buffer_pos(pos_type* batch_buffer_pos, uint64_t batch_buffer_size)
{
	xorshift32_state rnd;
	rnd.a = bigrand();
	
	for(uint64_t i=0; i<batch_buffer_size; i++)
		batch_buffer_pos[i] = xorshift32(&rnd) % TABLE_SIZE;		
		
}

void create_threads(int num_threads)
{
	pids = new pthread_t[num_threads];
	args = new worker_arg[num_threads];
	
	for (int i=0; i < num_threads; i++) {
		args[i].tid = i;						
		pthread_create(&pids[i], NULL, benchmark_ram_randomread_worker, &args[i]);
	}
		
}


void delete_threads(int num_threads)
{
	for (int i=0; i < num_threads; i++) {
		batch_work work;
		work.end = 0;	
		works_queue.enqueue(work);
	}
	
	for (int i=0; i < num_threads; i++) {
		pthread_join(pids[i], NULL);		
	}
	delete [] pids;
	delete [] args;	
}



uint64_t benchmark_ram_randomread_multithread(word_type* table, uint64_t batch_buffer_size, int num_threads, double& total_time)
{
	pos_type* batch_buffer_pos = new pos_type[batch_buffer_size];
	word_type* batch_buffer_words = new word_type[batch_buffer_size];
	
	gen_batch_buffer_pos(batch_buffer_pos, batch_buffer_size);

	
	auto start = high_resolution_clock::now(); 

	for (int i=0; i < num_threads; i++) {
		batch_work work;
		
		work.table = table;
		work.batch_buffer_pos = batch_buffer_pos;
		work.batch_buffer_words = batch_buffer_words;
		
		uint64_t worker_batch_size = batch_buffer_size / num_threads;
				
		work.start = worker_batch_size * i;
		work.end = work.start + worker_batch_size;
			
		works_queue.enqueue(work);		
	}
	
	/* oczekiwanie na zakończenie wszystkich wątków */
	for (int i=0; i < num_threads; i++) {
		batch_result res;
		res = results_queue.dequeue();						
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
	 	
	
	return sum;
}


int main(int argc, char* argv[])
{	
	int num_threads=32;
	int table_size_in_mib = 512;
	int copy_batch_size_in_kib = 1024;
	int num_iterations = 10;
	
	if (argc < 2)
	{
		printf("ramtest <num_threads> <table_size_in_mebibytes> <copy_batch_size_in_kibibytes> <num_iterations>\n");
		return 0;
	}
	
	num_threads = atoi(argv[1]);
	
	if (argc>2)	
		table_size_in_mib = atoi(argv[2]);
		
	TABLE_SIZE = table_size_in_mib * 1024UL * 1024UL / sizeof(word_type);
	
	
	if (argc>3)
		copy_batch_size_in_kib = atoi(argv[3]);
		
	BATCH_BUFFER_SIZE = copy_batch_size_in_kib * 1024UL / sizeof(word_type);
	
	
	if (argc > 4)
		num_iterations = atoi(argv[4]);

	
	printf("num_threads          : %d\n", num_threads);
	printf("table_size           : %d Mib\n", table_size_in_mib);
	printf("copy_batch_size      : %d Kib\n", copy_batch_size_in_kib);
	printf("num_iterations       : %d\n", num_iterations);
		
	word_type* table = create_table(TABLE_SIZE);	
	create_threads(num_threads);
	
	double sum_copy_per_sec;
	
	for(int i=0; i<num_iterations; i++)
	{									
		double workers_total_time = 0.0;		
		uint64_t sum = benchmark_ram_randomread_multithread(table, BATCH_BUFFER_SIZE, num_threads, workers_total_time);				
		double copy_per_sec = BATCH_BUFFER_SIZE / workers_total_time;		
		printf("[%d] checksum=%lu workers_time=%f copy_per_sec=%f\n", i+1, sum, workers_total_time, copy_per_sec);				
		sum_copy_per_sec += copy_per_sec;		
	}
	
	printf("avg_copy_per_sec %f\n", sum_copy_per_sec / num_iterations);

	delete_threads(num_threads);
	//delete [] table;	
	free(table)
	return 0;
}
