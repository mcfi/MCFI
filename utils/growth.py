#!/usr/bin/python

# This python script parses the profiling results of MCFI-hardened
# programs and out the growth.

import sys

def get_time(s):
    tm = s.split(']')[0]
    tm = tm[1:]
    sec = int(tm.split(':')[0])
    usec = int(tm.split(':')[1])
    return sec * 10**6 + usec

def main(prof, period = 10**6):
    start = 0
    end = 0
    growth = list()
    last_prof_point = start
    ibts = 0

    with open(prof) as f:
        for line in f:
            line = line.strip()
            if line.endswith('Started!'):
                start = get_time(line)
                last_prof_point = start
            elif line.endswith('Ended!'):
                end = get_time(line)
                ibts += 1
                growth.append(ibts)
                break
            else:
                cur = get_time(line)
                interval = cur - last_prof_point
                while interval > period:
                    growth.append(ibts)
                    last_prof_point += period
                    interval -= period
                ibts += 1

    for i in range(len(growth)):
        print '%d %d' % (i, growth[i])

if __name__=='__main__':
    if len(sys.argv) < 2:
        print 'growth.py [profiling.out] [period in microseconds, one second (10^6) by default]'
    period = 10**6
    if len(sys.argv) == 3:
        period = int(sys.argv[2])
    main(sys.argv[1], period)
