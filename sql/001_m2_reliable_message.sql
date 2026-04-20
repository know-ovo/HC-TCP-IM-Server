CREATE DATABASE IF NOT EXISTS `im_server`
  DEFAULT CHARACTER SET utf8mb4
  DEFAULT COLLATE utf8mb4_unicode_ci;

USE `im_server`;

CREATE TABLE IF NOT EXISTS `im_session` (
  `session_id` VARCHAR(128) NOT NULL COMMENT '会话ID',
  `user_id` VARCHAR(64) NOT NULL COMMENT '用户ID',
  `device_id` VARCHAR(64) NOT NULL DEFAULT '' COMMENT '设备ID',
  `server_node_id` VARCHAR(64) NOT NULL DEFAULT 'single-node' COMMENT '节点ID',
  `last_acked_seq` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '最近确认序列',
  `created_at_ms` BIGINT NOT NULL COMMENT '创建时间戳(ms)',
  `expires_at_ms` BIGINT NOT NULL COMMENT '过期时间戳(ms)',
  PRIMARY KEY (`session_id`),
  KEY `idx_user_id` (`user_id`),
  KEY `idx_expires_at` (`expires_at_ms`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='会话表';

CREATE TABLE IF NOT EXISTS `im_message` (
  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '全局消息ID',
  `client_msg_id` VARCHAR(64) NOT NULL DEFAULT '' COMMENT '客户端幂等消息ID',
  `from_user_id` VARCHAR(64) NOT NULL COMMENT '发送方用户ID',
  `to_user_id` VARCHAR(64) NOT NULL COMMENT '接收方用户ID',
  `conversation_id` VARCHAR(128) NOT NULL DEFAULT '' COMMENT '会话ID，单聊可扩展为 from:to',
  `payload_format` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=json 1=protobuf',
  `content` MEDIUMTEXT NOT NULL COMMENT '消息内容',
  `message_type` VARCHAR(32) NOT NULL DEFAULT 'text' COMMENT '消息类型',
  `status` TINYINT UNSIGNED NOT NULL DEFAULT 1 COMMENT '消息状态',
  `created_at_ms` BIGINT NOT NULL COMMENT '创建时间戳(ms)',
  `updated_at_ms` BIGINT NOT NULL COMMENT '更新时间戳(ms)',
  PRIMARY KEY (`id`),
  UNIQUE KEY `uk_from_client_msg` (`from_user_id`, `client_msg_id`),
  KEY `idx_to_created` (`to_user_id`, `created_at_ms`),
  KEY `idx_conversation_created` (`conversation_id`, `created_at_ms`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='消息主表';

CREATE TABLE IF NOT EXISTS `im_message_delivery` (
  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '投递记录ID',
  `message_id` BIGINT UNSIGNED NOT NULL COMMENT '关联消息ID',
  `receiver_user_id` VARCHAR(64) NOT NULL COMMENT '接收方用户ID',
  `server_seq` BIGINT UNSIGNED NOT NULL COMMENT '接收方递增序列',
  `status` TINYINT UNSIGNED NOT NULL DEFAULT 1 COMMENT '投递状态',
  `retry_count` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '投递重试次数',
  `last_error` VARCHAR(255) NOT NULL DEFAULT '' COMMENT '最后一次错误信息',
  `delivered_at_ms` BIGINT NOT NULL DEFAULT 0 COMMENT '最后投递时间戳(ms)',
  `acked_at_ms` BIGINT NOT NULL DEFAULT 0 COMMENT 'ACK 时间戳(ms)',
  `created_at_ms` BIGINT NOT NULL COMMENT '创建时间戳(ms)',
  `updated_at_ms` BIGINT NOT NULL COMMENT '更新时间戳(ms)',
  PRIMARY KEY (`id`),
  UNIQUE KEY `uk_receiver_seq` (`receiver_user_id`, `server_seq`),
  UNIQUE KEY `uk_message_receiver` (`message_id`, `receiver_user_id`),
  KEY `idx_receiver_status_seq` (`receiver_user_id`, `status`, `server_seq`),
  CONSTRAINT `fk_delivery_message_id`
    FOREIGN KEY (`message_id`) REFERENCES `im_message` (`id`)
    ON DELETE CASCADE
    ON UPDATE RESTRICT
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='消息投递状态表';

CREATE TABLE IF NOT EXISTS `im_message_ack_log` (
  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT 'ACK 日志ID',
  `message_id` BIGINT UNSIGNED NOT NULL COMMENT '消息ID',
  `receiver_user_id` VARCHAR(64) NOT NULL COMMENT '接收方用户ID',
  `server_seq` BIGINT UNSIGNED NOT NULL COMMENT '接收方序列',
  `ack_code` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=received 1=read',
  `acked_at_ms` BIGINT NOT NULL COMMENT 'ACK 时间戳(ms)',
  PRIMARY KEY (`id`),
  KEY `idx_receiver_seq` (`receiver_user_id`, `server_seq`),
  KEY `idx_message_ack` (`message_id`, `acked_at_ms`),
  CONSTRAINT `fk_ack_message_id`
    FOREIGN KEY (`message_id`) REFERENCES `im_message` (`id`)
    ON DELETE CASCADE
    ON UPDATE RESTRICT
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='消息 ACK 审计日志表';
