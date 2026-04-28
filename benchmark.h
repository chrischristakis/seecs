#pragma once
#include <chrono>
#include <vector>
#include "seecs.h"

class Timer {
private:

	using Clock = std::chrono::steady_clock;
	using Duration = Clock::duration;
	using TimePoint = Clock::time_point;

	TimePoint m_start;

public:

	Timer() {
		Reset();
	}

	// Return time in seconds since timer was last reset
	float Elapsed() {
		Duration duration = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - m_start);
		return duration.count() * 1e-9;
	}

	void Reset() {
		m_start = Clock::now();
	}

};

template <typename T>
struct Dummy {
	T data;
};

inline void RunBenchmark(const size_t I) {
	using namespace seecs;

	Timer t;
	ECS ecs;

	std::vector<Entity> entities;
	entities.resize(I);

	SEECS_MSG("Running 'creation' benchmark [" << I << "] entities");
	t.Reset();
	for (size_t i = 0; i < I; i++)
		entities[i] = ecs.CreateEntity();
	float elapsed = t.Elapsed();
	SEECS_MSG(" * " << elapsed << "s");

	SEECS_MSG("Running 'add component' benchmark [" << I << "] entities");
	t.Reset();
	for (size_t i = 0; i < I; i++) {
		ecs.Add<Dummy<int>>(entities[i], {});
	}
	elapsed = t.Elapsed();
	SEECS_MSG(" * " << elapsed << "s");

	SEECS_MSG("Running '.Get<Component>()' benchmark [" << I << "] entities");
	t.Reset();
	for (size_t i = 0; i < I; i++) {
		ecs.Get<Dummy<int>>(entities[i]);
	}
	elapsed = t.Elapsed();
	SEECS_MSG(" * " << elapsed << "s");

	SEECS_MSG("Running 'remove component' benchmark [" << I << "] entities");
	t.Reset();
	for (size_t i = 0; i < I; i++) {
		ecs.Remove<Dummy<int>>(entities[i]);
	}
	elapsed = t.Elapsed();
	SEECS_MSG(" * " << elapsed << "s");

	SEECS_MSG("Running 'delete entity' benchmark [" << I << "] entities");
	t.Reset();
	for (size_t i = 0; i < I; i++) {
		ecs.DeleteEntity(entities[i]);
	}
	elapsed = t.Elapsed();
	SEECS_MSG(" * " << elapsed << "s");

	ecs.Reset();
	for (size_t i = 0; i < I; i++) {
		entities[i] = ecs.CreateEntity();
		ecs.Add<Dummy<int>>(entities[i], {});
		ecs.Add<Dummy<double>>(entities[i], {});
	}

	SEECS_MSG("Running 'foreach (2 components)' benchmark [" << I << "] entities");
	auto view1 = ecs.View<Dummy<int>, Dummy<double>>();
	t.Reset();
	view1.ForEach([](Dummy<int>& _, Dummy<double>& __) {});
	elapsed = t.Elapsed();
	SEECS_MSG(" * " << elapsed << "s");

	SEECS_MSG("Running '.Get<...>() (2 components)' benchmark [" << I << "] entities");
	t.Reset();
	for (size_t i = 0; i < I; i++) {
		ecs.Get<Dummy<int>>(entities[i]);
		ecs.Get<Dummy<double>>(entities[i]);
	}
	elapsed = t.Elapsed();
	SEECS_MSG(" * " << elapsed << "s");

	ecs.Reset();
	for (size_t i = 0; i < I; i++) {
		entities[i] = ecs.CreateEntity();
		ecs.Add<Dummy<int>>(entities[i], {});
		ecs.Add<Dummy<double>>(entities[i], {});
		ecs.Add<Dummy<long>>(entities[i], {});
		ecs.Add<Dummy<float>>(entities[i], {});
	}

	SEECS_MSG("Running 'foreach (4 components)' benchmark [" << I << "] entities");
	
	auto view2 = ecs.View<Dummy<int>, Dummy<double>, Dummy<long>, Dummy<float>>();
	t.Reset();
	view2.ForEach([](Dummy<int>& _, Dummy<double>& __, Dummy<long>& ___, Dummy<float>& ____) {});
	elapsed = t.Elapsed();
	SEECS_MSG(" * " << elapsed << "s");

	SEECS_MSG("Running '.Get<...>() (4 components)' benchmark [" << I << "] entities");
	t.Reset();
	for (size_t i = 0; i < I; i++) {
		ecs.Get<Dummy<int>>(entities[i]);
		ecs.Get<Dummy<double>>(entities[i]);
		ecs.Get<Dummy<long>>(entities[i]);
		ecs.Get<Dummy<float>>(entities[i]);
	}
	elapsed = t.Elapsed();
	SEECS_MSG(" * " << elapsed << "s");


}