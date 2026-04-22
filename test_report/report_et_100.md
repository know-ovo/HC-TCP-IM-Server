========================================
       TCP Server Performance Report
========================================

## Test Configuration
| Parameter | Value |
|-----------|-------|
| Workload | full |
| Concurrent Clients | 100 |
| Test Duration | 60 seconds |
| Request Interval | 5000 us |
| Read Timeout | 1000 ms |

## Warmup Metrics
| Metric | Value |
|--------|-------|
| Warmup Ready Clients | 100 |
| Warmup Failed Clients | 0 |
| Warmup Login Requests | 100 |
| Warmup Login Timeouts | 0 |
| Warmup Login Errors | 0 |

## Throughput Metrics
| Metric | Value |
|--------|-------|
| Total Requests | 1523556 |
| Total Business Rounds | 345971 |
| Total Errors | 1 |
| Total Timeouts | 0 |
| Error Rate | 0.00% |
| Timeout Rate | 0.00% |
| Elapsed Time | 60.91 s |
| Request QPS | 25011.85 |
| Business Round QPS | 5679.72 |
| Avg Requests Per Round | 4.40 |
| Throughput | 2581.68 KB/s |

## Error Breakdown
| Type | Count |
|------|-------|
| SocketSetup | 0 |
| Connect | 0 |
| Send | 0 |
| Receive | 0 |
| Protocol | 1 |

## Protocol Error Breakdown
| Operation | Protocol Errors |
|-----------|-----------------|
| Login | 0 |
| Heartbeat | 0 |
| P2P | 0 |
| PullOffline | 0 |
| ACK | 1 |

## Recent Protocol Errors
- ACK rejected user=bench_user_50 msg_id=5913 server_seq=136 code=1 msg=ack user mismatch

## Business Flow Metrics
| Operation | Requests | Timeouts | Errors |
|-----------|----------|----------|--------|
| Login | 0 | 0 | 0 |
| Heartbeat | 345972 | 0 | 0 |
| P2P | 345972 | 0 | 0 |
| PullOffline | 345972 | 0 | 0 |
| ACK | 485640 | 0 | 1 |

## Latency Metrics (ms)
| Metric | Value |
|--------|-------|
| Min | 0.496 |
| Max | 1298.144 |
| Avg | 12.026 |
| P50 | 1.809 |
| P90 | 5.695 |
| P95 | 48.249 |
| P99 | 293.179 |
