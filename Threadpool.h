#ifndef _THREADPOOL_H_
#define _THREADPOOL_H_

#include "Packdef.h"

using namespace std;


//任务队列属性
class ThreadPool
{
	private://私有化构造函数
		ThreadPool();
	public:
		~ThreadPool();
	public://线程池
		int thread_min;							//线程组内默认最小线程数
		int thread_max;							//线程组内默认最大线程数
		int thread_alive;						//当前存活线程数
		int thread_busy;						//当前忙状态线程数
		int thread_kill;						//等待退出线程数
		bool shutdown;							//线程池使用的状态
	public://任务队列
		struct task_t
		{
			void* (*function)(void*);			//函数指针，回调函数
			void* arg;							//上面函数的参数
		};
		int queue_head;							//队头索引
		int queue_rear;							//队尾索引
		int queue_cur;							//队列中元素个数
		int queue_max;							//队列中最大容纳个数
		task_t* task_queue;						//任务队列
		pthread_t* workers_tid;					//保存工作线程tid的数组
		pthread_t manager_tid;					//管理线程
	public://线程同步
		pthread_mutex_t lock;					//锁
		pthread_cond_t queue_not_full;			//条件变量，用于记录队列不再为满
		pthread_cond_t queue_not_empty;			//用于记录队列不再为空
	public:
		static void* Worker(void* arg);			//工作线程函数
		static void* Manager(void* arg);		//主控线程函数
		bool AddTask(void*(*function)(void*arg),void* arg);	//添加任务到任务队列
	public:
		static ThreadPool* m_instance;
	public:
		static ThreadPool* GetInstance()
		{
			for(;;){
				if(m_instance)
					return m_instance;
				ThreadPool* threadpool = new ThreadPool();
				if(!m_instance){
					if(__sync_bool_compare_and_swap(&m_instance,NULL,threadpool)){
						//如果m_instance是空，则把threadpool写入m_instance
						//threadpool赋空，返回m_instance
						threadpool = NULL;
						return m_instance;
					}else{
						//如果m_instance不为空，删除threadpool赋空
						delete threadpool;
						threadpool = NULL;
					}
				}

			}
		}
	public:
		bool InitPool(int min,int max,int queue_max);
		void FreePool();							//释放线程池
		void DestroyPool();							//销毁线程池
		bool is_alive(pthread_t tid);				//判断线程是否存活
		int GetAllThreadCount();					//获取存活线程个数
		int GetBusyThreadCount();					//获取忙线程个数
};
#endif
