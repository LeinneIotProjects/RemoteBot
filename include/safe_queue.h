#pragma once

#include <queue>
#include <mutex>
#include <utility>
#include <condition_variable>

using namespace std;

template <class T>
class SafeQueue{
private:
	queue<T> _queue;
	mutable mutex _mutex;
	condition_variable _condition;
    
public:
	SafeQueue(): _queue(), _mutex(), _condition(){}
	~SafeQueue(){}

	void waitPush(){
		unique_lock<mutex> lock(_mutex);
		while(_queue.empty()){
			_condition.wait(lock);
		}
	}

	void push(T t){
		lock_guard<mutex> lock(_mutex);
		_queue.push(t);
		_condition.notify_one();
	}

	bool empty(){
		lock_guard<mutex> lock(_mutex);
		return _queue.empty();
	}

	size_t size(){
		lock_guard<mutex> lock(_mutex);
		return _queue.size();
	}

	T pop(){
		unique_lock<mutex> lock(_mutex);
		while(_queue.empty()){
			_condition.wait(lock);
		}
		T val = _queue.front();
		_queue.pop();
		return val;
	}
};