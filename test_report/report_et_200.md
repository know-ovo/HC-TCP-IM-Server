========================================
       TCP Server Performance Report
========================================

## Test Configuration
| Parameter | Value |
|-----------|-------|
| Workload | full |
| Concurrent Clients | 200 |
| Test Duration | 60 seconds |
| Request Interval | 5000 us |
| Read Timeout | 1000 ms |

## Warmup Metrics
| Metric | Value |
|--------|-------|
| Warmup Ready Clients | 200 |
| Warmup Failed Clients | 0 |
| Warmup Login Requests | 200 |
| Warmup Login Timeouts | 0 |
| Warmup Login Errors | 0 |

## Throughput Metrics
| Metric | Value |
|--------|-------|
| Total Requests | 2022164 |
| Total Business Rounds | 513010 |
| Total Errors | 2 |
| Total Timeouts | 0 |
| Error Rate | 0.00% |
| Timeout Rate | 0.00% |
| Elapsed Time | 60.91 s |
| Request QPS | 33197.28 |
| Business Round QPS | 8421.94 |
| Avg Requests Per Round | 3.94 |
| Throughput | 3499.96 KB/s |

## Error Breakdown
| Type | Count |
|------|-------|
| SocketSetup | 0 |
| Connect | 0 |
| Send | 0 |
| Receive | 0 |
| Protocol | 2 |

## Protocol Error Breakdown
| Operation | Protocol Errors |
|-----------|-----------------|
| Login | 0 |
| Heartbeat | 0 |
| P2P | 0 |
| PullOffline | 0 |
| ACK | 2 |

## Recent Protocol Errors
- ACK rejected user=bench_user_100 msg_id=156487 server_seq=2992 code=1 msg=ack user mismatch
- ACK rejected user=bench_user_50 msg_id=25948 server_seq=554 code=1 msg=ack user mismatch

## Business Flow Metrics
| Operation | Requests | Timeouts | Errors |
|-----------|----------|----------|--------|
| Login | 0 | 0 | 0 |
| Heartbeat | 513012 | 0 | 0 |
| P2P | 513012 | 0 | 0 |
| PullOffline | 513012 | 0 | 0 |
| ACK | 483128 | 0 | 2 |

## Latency Metrics (ms)
| Metric | Value |
|--------|-------|
| Min | 0.503 |
| Max | 1374.144 |
| Avg | 18.065 |
| P50 | 5.735 |
| P90 | 9.276 |
| P95 | 12.204 |
| P99 | 708.451 |
