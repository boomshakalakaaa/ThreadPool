#include "Threadpool.cpp"

//测试用
#if 1
void* process(void* arg)
{
	printf("thread %ld working on task%d\n",pthread_self(),*(int*)arg);
	sleep(1);
	printf("task %d is end\n",*(int *)arg);
	return NULL;
}

int main(void)
{
	ThreadPool* tp = ThreadPool::GetInstance();
	if(tp->InitPool(10,200,1000)){
		printf("ThreadPool inited...\n");
		sleep(5);
	}
	else
		printf("ThreadPool init failed!\n");
	int* num = (int*)malloc(sizeof(int)*20000);
	int i;
	for(i = 0;i < 100;i++){
		num[i] = i;
		printf("add task %d\n",i);
		tp->AddTask(process,(void*)&num[i]);
	}
	while(tp){
		sleep(1);
		printf("Current thread_alive count is : %d===============================\n",tp->GetAllThreadCount());
		printf("Current thread_busy count is : %d================================\n",tp->GetBusyThreadCount());
	}
	//sleep(30);
	return 0;
}

#endif
