#include <stddef.h>
#include <time.h>
#include "brubeck.h"

void brubeck_backend_register_metric(struct brubeck_backend *self, struct brubeck_metric *metric)
{
	for (;;) {
		struct brubeck_metric *next = self->queue;
		metric->next = next;

		if (__sync_bool_compare_and_swap(&self->queue, next, metric))
			break;
	}
}

static void
expire_metric(struct brubeck_metric *mt, void *_)
{
  /* If this metric is not disabled, turn "inactive"
   * into "disabled" and "active" into "inactive"
   */
  if (mt->expire > BRUBECK_EXPIRE_ACTIVE) return;
  if (mt->expire == BRUBECK_EXPIRE_ACTIVE)
    {
      mt->expire = BRUBECK_EXPIRE_INACTIVE;
      mt->as.counter.value = mt->as.meter.value = mt->as.gauge.value = 0;
    }
}

static void *backend__thread(void *_ptr)
{
	struct brubeck_backend *self = (struct brubeck_backend *)_ptr;
	struct timespec now, then;

	clock_gettime(CLOCK_MONOTONIC, &then);
	for (;;) {

		//clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &then, NULL);
		//		clock_nanosleep_abstime(&then, NULL);
		//		clock_gettime(CLOCK_MONOTONIC, &then);

		if (!self->connect(self)) {
			struct brubeck_metric *mt;

			//			clock_gettime(CLOCK_REALTIME, &now);
			//			self->tick_time = now.tv_sec;

			for (mt = self->queue; mt; mt = mt->next)
			  {
			    if (mt->expire > BRUBECK_EXPIRE_INACTIVE)
			      brubeck_metric_sample(mt, self->sample, self);
			    if (self->expire)
				expire_metric(mt, NULL);
			}

			if (self->flush)
				self->flush(self);
		}

		now = then;
		then.tv_sec += self->sample_freq;
		clock_nanosleep_abstime(&then, NULL);
		//		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &then, NULL);
	}
	return NULL;
}

void brubeck_backend_run_threaded(struct brubeck_backend *self)
{
	if (pthread_create(&self->thread, NULL, &backend__thread, self) != 0)
		die("failed to start backend thread");
}

