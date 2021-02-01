#include"Threadpool.h"
ThreadPool* ThreadPool::m_instance = NULL;

ThreadPool::ThreadPool()
{
	//线程池
	this->shutdown = false;
	this->thread_busy = 0;
	this->thread_kill = 0;
	this->thread_alive = 0;
	//任务队列
	this->queue_head = 0;
	this->queue_rear = 0;
	this->queue_cur = 0;
}
ThreadPool::~ThreadPool()
{
	this->DestroyPool();

}

//初始化线程池
bool ThreadPool::InitPool(int min,int max,int queue_max)
{
	//线程池的变量
	this->thread_min = min;
	this->thread_max = max;

	//任务队列的变量
	this->queue_max = queue_max;

	//申请工作线程数组空间，清零
	this->workers_tid = new pthread_t[max];
	if(this->workers_tid == NULL){
		perror("Threadpool.cpp->Workers array memory init failed\n");
		return false;
	}
	bzero(this->workers_tid,sizeof(pthread_t)*max);

	//申请任务队列的空间，清零
	this->task_queue = new task_t[queue_max];
	if(this->task_queue == NULL){
		perror("Threadpool.cpp->Task_queue array memory init failed\n");
		return false;
	}
	bzero(this->task_queue,sizeof(task_t)*queue_max);

	//初始化锁、条件变量
	if(pthread_mutex_init(&(this->lock),NULL) != 0 
		|| pthread_cond_init(&(this->queue_not_empty),NULL) != 0 
		|| pthread_cond_init(&(this->queue_not_full),NULL) != 0)
	{
		perror("Threadpool.cpp->Init the lock or cond failed\n");
		return false;
	}
	//创建主控线程
	this->manager_tid = 0;
	if(pthread_create(&(this->manager_tid),NULL,Manager,(void*)this) != 0){
		perror("Threadpool.cpp->Create manager failed\n");
		return false;
	}
	//根据线程池最小线程数创建线程
	for(int i = 0;i < min;i++){
		if(pthread_create(&(this->workers_tid[i]),NULL,Worker,(void*)this) == 0){
			printf("Thread [%ld] is created...\n",this->workers_tid[i]);
			++(this->thread_alive);
		}else{
			printf("Threadpool.cpp->Create worker num [%d] failed\n",i);	
			return false;
		}
	}
	return true;
}

//添加任务（生产者）
bool ThreadPool::AddTask(void*(*function)(void*arg),void* arg)
{
	if(!function){
		perror("Threadpool.cpp->AddTask arg is null\n");
		return false;
	}
	pthread_mutex_lock(&(this->lock));
	if(!this->shutdown){
		while(this->queue_cur == this->queue_max){
			//此处用while而不是if，防止虚假唤醒
			pthread_cond_wait(&(this->queue_not_full),&(this->lock));
			//再次判断线程池在等待条件变量过程中是否终止运行
			if(this->shutdown){
				perror("Threadpool.cpp->Threadpool is shutdown , thread exit\n");
				pthread_mutex_unlock(&(this->lock));
				return false;
				//pthread_exit((void*)0);
			}
		}

		//将任务添加到环形队列中
		this->task_queue[this->queue_head].function = function;
		this->task_queue[this->queue_head].arg = arg;
		this->queue_head = (this->queue_head + 1) % this->queue_max;
		this->queue_cur++;

		//释放条件变量和锁，通知其他线程，队列中有任务
		pthread_cond_signal(&(this->queue_not_empty));
	}
	pthread_mutex_unlock(&(this->lock));
	return true;
}

//工作线程（消费者）
void* ThreadPool::Worker(void* arg)
{
	if(arg == NULL){
		perror("Threadpool.cpp->Worker arg is NULL");
		return NULL;
	}
	pthread_detach(pthread_self());

	ThreadPool* pThis = (ThreadPool*)arg;
	task_t task;
	
	while(!pThis->shutdown){
		//刚创建出线程，等待任务队列里有任务，否则阻塞等待任务队列里有任务后再唤醒接收任务
		printf("Worker: TaskQueue is empty , thread =======[%ld]======= is waiting for task\n",pthread_self());
		pthread_mutex_lock(&(pThis->lock));
		while((pThis->queue_cur == 0)){
			pthread_cond_wait(&(pThis->queue_not_empty),&(pThis->lock));
			if(pThis->shutdown || pThis->thread_kill > 0){
				pThis->thread_alive--;
				pThis->thread_kill--;
				pthread_mutex_unlock(&pThis->lock);
				printf("Worker: Thread =======[%ld]======= is exiting\n",pthread_self());
				pthread_exit(NULL);

			}
		}
		//从任务队列里获得任务
		task.function = pThis->task_queue[pThis->queue_rear].function;
		task.arg = pThis->task_queue[pThis->queue_rear].arg;
		pThis->queue_cur--;
		pThis->queue_rear = (pThis->queue_rear + 1) % pThis->queue_max;
		//通知可以有新任务添加进来
		pthread_cond_signal(&(pThis->queue_not_full));
		pThis->thread_busy++;

		pthread_mutex_unlock(&(pThis->lock));

		//执行任务
		printf("Worker: Thread =======[%ld]======= starts working\n",pthread_self());
		(*(task.function))(task.arg);
		
		//任务结束处理
		printf("Worker: Thread =======[%ld]======= ends working\n",pthread_self());
		pthread_mutex_lock(&(pThis->lock));
		pThis->thread_busy--;
		pthread_mutex_unlock(&(pThis->lock));
	}
	pthread_exit(NULL);
	return NULL;

}

//管理线程
void* ThreadPool::Manager(void* arg)
{
	if(arg == NULL){
		perror("Threadpool.cpp->Manager arg is NULL");
		return NULL;
	}
	pthread_detach(pthread_self());

	ThreadPool* pThis = (ThreadPool*)arg;
	
	int alive = 0;
	int size = 0;
	int busy = 0;
	int max = pThis->thread_max;
	int min = pThis->thread_min;

	while(!pThis->shutdown){
		//加锁 获取	当前存活线程数和忙线程数
		printf("Manager =======[%ld]======= is working......===============================\n",pthread_self());
		pthread_mutex_lock(&(pThis->lock));
		alive = pThis->thread_alive;
		busy = pThis->thread_busy;
		size = pThis->queue_cur;
		pthread_mutex_unlock(&(pThis->lock));

		//忙线程占存活线程的70% 并且 最大线程个数 大于等于 存活线程数+最小线程数时，扩容
		if((size > alive - busy || busy * 100 / alive >= 80) && alive + min <= max){
			//一次扩容min个
			for(int i = 0; i < min; i++){
				//线性探测线程id数组中的线程是否存活，对数组进行复用
				for(int j = 0; j < max; j++){
					if(pThis->workers_tid[j] == 0 || pThis->is_alive(pThis->workers_tid[j])){				
						pthread_mutex_lock(&(pThis->lock));
						pthread_create(&(pThis->workers_tid[j]),NULL,Worker,(void*)pThis);
						pThis->thread_alive++;
						printf("In manager ===========================thread_alive:%d......===============================\n",alive);
						pthread_mutex_unlock(&(pThis->lock));
						break;
					}
				}
			}
		}
		//销毁多余的空闲线程
		if(busy * 2 < alive - size && alive - min >= min){
			pthread_mutex_lock(&(pThis->lock));
			pThis->thread_kill = DEFAULT_THREAD_VARY;

			for(int i = 0;i < DEFAULT_THREAD_VARY;i++){
				pthread_cond_signal(&(pThis->queue_not_empty));
			}
			pthread_mutex_unlock(&(pThis->lock));
		}
		sleep(1);
	}
	return NULL;
}

void ThreadPool::DestroyPool()
{
	//终止标志置为true
	pthread_mutex_lock(&lock);
	this->shutdown = true;
	pthread_mutex_unlock(&lock);

	//销毁管理线程
	pthread_join(this->manager_tid,NULL);
	//通知所有的空闲线程
	pthread_cond_broadcast(&(this->queue_not_empty));
	for(int i = 0;i < this->thread_min;i++)
		pthread_join(this->workers_tid[i],NULL);

	this->FreePool();
}

void ThreadPool::FreePool()
{
	//释放任务队列和线程数组、锁、条件变量
	if(this->task_queue)
		delete [](this->task_queue);
	if(this->workers_tid)
		delete [](this->workers_tid);
	sleep(5);
	pthread_mutex_destroy(&(this->lock));
	pthread_cond_destroy(&(this->queue_not_empty));
	pthread_cond_destroy(&(this->queue_not_full));
	
}

bool ThreadPool::is_alive(pthread_t tid)
{
	int kill_rc = pthread_kill(tid,0);
	if(kill_rc == ESRCH)
		return false;
	return true;
}
		
int ThreadPool::GetAllThreadCount()
{
	int all_threadnum = 1;
	pthread_mutex_lock(&(this->lock));
	all_threadnum = this->thread_alive;
	pthread_mutex_unlock(&(this->lock));
	return all_threadnum;
}

int ThreadPool::GetBusyThreadCount()
{
	int busy_threadnum = -1;
	pthread_mutex_lock(&(this->lock));
	busy_threadnum = this->thread_busy;
	pthread_mutex_unlock(&(this->lock));
	return busy_threadnum;
}
