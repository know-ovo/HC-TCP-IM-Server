========================================
       TCP Server Performance Report
========================================

## Test Configuration
| Parameter | Value |
|-----------|-------|
| Workload | full |
| Concurrent Clients | 500 |
| Test Duration | 60 seconds |
| Request Interval | 5000 us |
| Read Timeout | 1000 ms |

## Warmup Metrics
| Metric | Value |
|--------|-------|
| Warmup Ready Clients | 500 |
| Warmup Failed Clients | 0 |
| Warmup Login Requests | 500 |
| Warmup Login Timeouts | 0 |
| Warmup Login Errors | 0 |

## Throughput Metrics
| Metric | Value |
|--------|-------|
| Total Requests | 2200529 |
| Total Business Rounds | 535076 |
| Total Errors | 3 |
| Total Timeouts | 0 |
| Error Rate | 0.00% |
| Timeout Rate | 0.00% |
| Elapsed Time | 60.92 s |
| Request QPS | 36121.87 |
| Business Round QPS | 8783.32 |
| Avg Requests Per Round | 4.11 |
| Throughput | 3804.46 KB/s |

## Error Breakdown
| Type | Count |
|------|-------|
| SocketSetup | 0 |
| Connect | 0 |
| Send | 0 |
| Receive | 0 |
| Protocol | 3 |

## Protocol Error Breakdown
| Operation | Protocol Errors |
|-----------|-----------------|
| Login | 0 |
| Heartbeat | 0 |
| P2P | 0 |
| PullOffline | 0 |
| ACK | 3 |

## Recent Protocol Errors
- ACK rejected user=bench_user_200 msg_id=579285 server_seq=9888 code=1 msg=ack user mismatch
- ACK rejected user=bench_user_50 msg_id=68381 server_seq=1481 code=1 msg=ack user mismatch
- ACK rejected user=bench_user_100 msg_id=211319 server_seq=3711 code=1 msg=ack user mismatch

## Business Flow Metrics
| Operation | Requests | Timeouts | Errors |
|-----------|----------|----------|--------|
| Login | 0 | 0 | 0 |
| Heartbeat | 535079 | 0 | 0 |
| P2P | 535079 | 0 | 0 |
| PullOffline | 535079 | 0 | 0 |
| ACK | 595292 | 0 | 3 |

## Latency Metrics (ms)
| Metric | Value |
|--------|-------|
| Min | 0.872 |
| Max | 2787.250 |
| Avg | 50.751 |
| P50 | 21.705 |
| P90 | 30.326 |
| P95 | 42.035 |
| P99 | 1650.252 |
