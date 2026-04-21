========================================
       TCP Server Performance Report
====================================

# baseline:

## Test Configuration

| Parameter          | Value       |
| ------------------ | ----------- |
| Server Address     | (see log)   |
| Concurrent Clients | 500         |
| Test Duration      | 120 seconds |
| Request Interval   | 500 μs     |

## Throughput Metrics

| Metric         | Value        |
| -------------- | ------------ |
| Total Messages | 5370344      |
| Total Errors   | 0            |
| Error Rate     | 0.00%        |
| Elapsed Time   | 130.13 s     |
| QPS            | 41270.33     |
| Throughput     | 2539.09 KB/s |

## Latency Metrics (ms)

| Metric      | Value  |
| ----------- | ------ |
| Min Latency | 0.103  |
| Max Latency | 61.429 |
| Avg Latency | 10.550 |
| P50 Latency | 10.375 |
| P90 Latency | 13.001 |
| P95 Latency | 19.397 |
| P99 Latency | 24.030 |

## Performance Rating

| Metric      | Value     | Rating    |
| ----------- | --------- | --------- |
| QPS         | 41270     | Good      |
| Avg Latency | 10.550 ms | Fair      |
| P99 Latency | 24.030 ms | Fair      |
| Error Rate  | 0.00%     | Excellent |

========================================
Report generated at: Apr  5 2026 21:09:56


# optiimized_1


## Test Configuration

| Parameter          | Value       |
| ------------------ | ----------- |
| Concurrent Clients | 500         |
| Test Duration      | 120 seconds |
| Request Interval   | 500 μs     |

## Throughput Metrics

| Metric         | Value       |
| -------------- | ----------- |
| Total Messages | 5272257     |
| Total Errors   | 0           |
| Error Rate     | 0.00%       |
| Elapsed Time   | 120.07 s    |
| QPS            | 43908.31    |
| Throughput     | 771.83 KB/s |

## Latency Metrics (ms)

| Metric      | Value    |
| ----------- | -------- |
| Min Latency | 0.202    |
| Max Latency | 2286.837 |
| Avg Latency | 10.752   |
| P50 Latency | 9.980    |
| P90 Latency | 12.260   |
| P95 Latency | 18.962   |
| P99 Latency | 22.840   |

## Performance Rating

| Metric      | Value     | Rating    |
| ----------- | --------- | --------- |
| QPS         | 43908     | Good      |
| Avg Latency | 10.752 ms | Fair      |
| P99 Latency | 22.840 ms | Fair      |
| Error Rate  | 0.00%     | Excellent |

========================================
Report generated at: Mon Apr  6 15:17:18 2026

出现问题：最大延迟变化：+3620%

问题原因：Redis 操作是同步阻塞的 ，在心跳处理中直接调用会阻塞事件循环线程。。在当前单机、简单心跳测试场景下，Redis 带来的提升有限（QPS +6.4%），但引入了偶发性延迟尖峰。Redis 的真正价值在于 集群部署 和 状态共享 场景。

解决方案：将 Redis 操作异步化，放到线程池中执行


# optimized_2:


## Test Configuration

| Parameter          | Value       |
| ------------------ | ----------- |
| Concurrent Clients | 500         |
| Test Duration      | 120 seconds |
| Request Interval   | 500 μs     |

## Throughput Metrics

| Metric         | Value       |
| -------------- | ----------- |
| Total Messages | 5446918     |
| Total Errors   | 0           |
| Error Rate     | 0.00%       |
| Elapsed Time   | 120.08 s    |
| QPS            | 45359.84    |
| Throughput     | 797.34 KB/s |

## Latency Metrics (ms)

| Metric      | Value    |
| ----------- | -------- |
| Min Latency | 0.203    |
| Max Latency | 2323.425 |
| Avg Latency | 10.396   |
| P50 Latency | 9.618    |
| P90 Latency | 11.877   |
| P95 Latency | 18.431   |
| P99 Latency | 21.938   |

## Performance Rating

| Metric      | Value     | Rating    |
| ----------- | --------- | --------- |
| QPS         | 45360     | Good      |
| Avg Latency | 10.396 ms | Fair      |
| P99 Latency | 21.938 ms | Fair      |
| Error Rate  | 0.00%     | Excellent |

========================================

出现问题：最大延迟变化+3680%，问题不变

问题原因：异步化解决了大部分阻塞，但仍有偶发性延迟尖峰。
具体可能原因：

1. 测试客户端问题 ⭐ 最可能
   ├── 同步阻塞读取响应
   │   ssize_t bytesRead = read(sockfd, readBuf, sizeof(readBuf));
   │                       ↑ 阻塞等待
   ├── 如果服务器响应慢，客户端也会被阻塞
   └── 单个慢响应会影响后续请求计时
2. Redis 连接问题
   ├── 单连接瓶颈
   ├── hiredis 同步 API 内部可能有阻塞
   └── 连接断开重连耗时
3. 系统因素
   ├── Linux 调度延迟
   ├── 内存分配
   └── 网络抖动

解决方法：将客户端的读取操作从阻塞改为非阻塞加超市限制

Report generated at: Mon Apr  6 20:10:54 2026


# optimized_3:


## Test Configuration

| Parameter          | Value       |
| ------------------ | ----------- |
| Concurrent Clients | 500         |
| Test Duration      | 120 seconds |
| Request Interval   | 500 μs     |
| Read Timeout       | 100 ms      |

## Throughput Metrics

| Metric         | Value       |
| -------------- | ----------- |
| Total Messages | 5644652     |
| Total Errors   | 0           |
| Total Timeouts | 4408        |
| Error Rate     | 0.00%       |
| Timeout Rate   | 0.08%       |
| Elapsed Time   | 120.10 s    |
| QPS            | 46999.93    |
| Throughput     | 826.17 KB/s |

## Latency Metrics (ms)

| Metric      | Value   |
| ----------- | ------- |
| Min Latency | 0.004   |
| Max Latency | 102.889 |
| Avg Latency | 10.006  |
| P50 Latency | 12.010  |
| P90 Latency | 14.259  |
| P95 Latency | 14.951  |
| P99 Latency | 26.622  |

## Performance Rating

| Metric      | Value     | Rating    |
| ----------- | --------- | --------- |
| QPS         | 47000     | Good      |
| Avg Latency | 10.006 ms | Fair      |
| P99 Latency | 26.622 ms | Fair      |
| Error Rate  | 0.00%     | Excellent |

========================================
Report generated at: Mon Apr  6 21:05:13 2026


# 2026-04-21：ET vs LT 对比（server_full + benchmarkClient）

## 对比目的

本组数据用于回答两个问题：

- ET 与 LT 在低负载与高负载下的吞吐与尾延迟差异
- 是否存在“吞吐略升，但尾延迟变差”的典型权衡

## 测试说明

- 服务端：`build_linux/bin/server_full`
- 客户端：`build_linux/bin/benchmarkClient`（login_then_heartbeat 模式）
- 请求类型：登录后心跳请求/响应
- 读超时：`-t`（ms），登录超时：`--login-timeout`（ms）

## 结果汇总（模板）

| Case | Mode | Clients | Interval | Duration | QPS | Avg (ms) | P99 (ms) | Max (ms) | Errors | Timeouts |
|------|------|---------|----------|----------|-----|----------|----------|----------|--------|----------|
| c20_i0 | ET | 20 | 0 μs | 20s | 60554.52 | 0.326 | 0.714 | 5.354 | 0 | 0 |
| c20_i0 | LT | 20 | 0 μs | 20s | 56379.46 | 0.351 | 1.617 | 8.741 | 0 | 0 |
| c100_i1000 | ET | 100 | 1000 μs | 60s | 57507.58 | 0.581 | 1.961 | 15.373 | 0 | 0 |
| c500_i500 | ET | 500 | 500 μs | 120s | 83696.36 | 5.266 | 9.441 | 32.013 | 0 | 0 |
| c500_i500 | LT | 500 | 500 μs | 120s | 84722.78 | 5.183 | 11.404 | 77.210 | 0 | 0 |

## 结论（如何讲）

1. 低负载（c20_i0）下，ET 吞吐更高（约 +7%），但 LT 的尾延迟更差（P99 更高）。这符合 ET“减少重复唤醒”的收益预期。
2. 高负载（c500_i500）下，ET 与 LT 的吞吐非常接近（<2%），但 LT 的尾部尖峰更明显（Max 与 P99 更高）。这表明系统进入排队态后，尾延迟更受调度/排队影响，epoll 模式不再是单一决定因素。
3. 这组数据说明：优化不能只看 QPS，要同时看 P99/Max。吞吐接近时，应优先选择尾延迟更稳定的策略，避免线上偶发尖峰。
