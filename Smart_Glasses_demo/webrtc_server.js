const WebSocket = require('ws');
const http = require('http');

// 服务器配置参数
const CONFIG = {
    PORT: 8000,
    ROOM_TIMEOUT: 30000, // 30秒房间超时
    MAX_CONNECTIONS: 1000,
    MESSAGE_QUEUE_SIZE: 10000,
    LOG_LEVEL: 'INFO'
};

// 错误码定义
const ERROR_CODES = {
    ROOM_FULL: 1001,
    ROOM_NOT_EXISTS: 1002,
    MESSAGE_FORMAT_ERROR: 1003,
    DEVICE_ID_ERROR: 1004,
    CONNECTION_TIMEOUT: 1005,
    PEER_OFFLINE: 1006,
    SERVER_ERROR: 1007
};

// 房间状态枚举
const ROOM_STATUS = {
    WAITING: 'WAITING',
    PAIRED: 'PAIRED',
    TIMEOUT: 'TIMEOUT'
};

// 连接状态枚举
const CONNECTION_STATUS = {
    CONNECTED: 'CONNECTED',
    JOINED: 'JOINED',
    PAIRED: 'PAIRED'
};

// 房间管理器
class RoomManager {
    constructor() {
        this.rooms = new Map(); // roomId -> Room对象
        this.connections = new Map(); // connectionId -> Connection对象
    }

    // 创建或加入房间
    joinRoom(connection, deviceId) {
        const roomId = this.extractRoomId(deviceId);
        
        if (!this.rooms.has(roomId)) {
            // 创建新房间
            const room = {
                id: roomId,
                status: ROOM_STATUS.WAITING,
                connections: [],
                createdAt: Date.now(),
                timeout: null
            };
            
            this.rooms.set(roomId, room);
            
            // 设置房间超时
            room.timeout = setTimeout(() => {
                this.handleRoomTimeout(roomId);
            }, CONFIG.ROOM_TIMEOUT);
            
            console.log(`[INFO] 创建房间: ${roomId}`);
        }

        const room = this.rooms.get(roomId);
        
        // 检查房间是否已满
        if (room.connections.length >= 2) {
            return this.sendError(connection, ERROR_CODES.ROOM_FULL, "房间已满，无法加入");
        }

        // 将连接添加到房间
        room.connections.push(connection);
        connection.roomId = roomId;
        connection.status = CONNECTION_STATUS.JOINED;
        
        console.log(`[INFO] 客户端 ${deviceId} 加入房间: ${roomId}`);

        // 发送房间信息变动通知
        this.sendRoomInfo(room);

        // 如果房间满了，进行配对
        if (room.connections.length === 2) {
            this.pairClients(room);
        }

        return true;
    }

    // 配对客户端
    pairClients(room) {
        if (room.connections.length !== 2) return;

        // 清除房间超时
        if (room.timeout) {
            clearTimeout(room.timeout);
            room.timeout = null;
        }

        room.status = ROOM_STATUS.PAIRED;
        
        const [client1, client2] = room.connections;
        
        // 第一个加入的是发起方(offerer)，第二个是应答方(answerer)
        client1.role = 'offerer';
        client2.role = 'answerer';
        
        client1.status = CONNECTION_STATUS.PAIRED;
        client2.status = CONNECTION_STATUS.PAIRED;

        // 发送角色信息给双方
        this.sendRoleMessage(client1, client2.deviceId);
        this.sendRoleMessage(client2, client1.deviceId);

        // 发送配对后的房间信息变动通知
        this.sendRoomInfo(room);

        console.log(`[INFO] 房间 ${room.id} 配对成功: ${client1.deviceId} <-> ${client2.deviceId}`);
    }

    // 发送角色信息
    sendRoleMessage(connection, peerDeviceId) {
        const message = {
            type: 'role',
            device_id: peerDeviceId,
            from: 'server',
            to: connection.deviceId,
            data: { peer_device_id: peerDeviceId, role: connection.role },
            time: this.getCurrentTimestamp()
        };
        
        this.sendMessage(connection, message);
    }

    // 发送房间信息变动消息
    sendRoomInfo(room) {
        if (!room || room.connections.length === 0) return;

        const roomInfo = {
            room_id: room.id,
            num: room.connections.length,
            room_status: room.status === ROOM_STATUS.PAIRED ? 'open' : 'close'
        };

        // 向房间内所有客户端发送房间信息
        room.connections.forEach(connection => {
            const message = {
                type: 'info',
                device_id: connection.deviceId,
                from: 'server',
                to: connection.deviceId,
                data: roomInfo,
                time: this.getCurrentTimestamp()
            };
            
            this.sendMessage(connection, message);
            console.log(`[INFO] 发送房间信息给 ${connection.deviceId}: 房间${room.id}, 人数${roomInfo.num}, 状态${roomInfo.room_status}`);
        });
    }

    // 离开房间
    leaveRoom(connection) {
        if (!connection.roomId) return;

        const room = this.rooms.get(connection.roomId);
        if (!room) return;

        // 从房间移除连接
        room.connections = room.connections.filter(conn => conn !== connection);
        
        console.log(`[INFO] 客户端 ${connection.deviceId} 离开房间: ${connection.roomId}`);

        // 如果房间为空，删除房间
        if (room.connections.length === 0) {
            if (room.timeout) {
                clearTimeout(room.timeout);
            }
            this.rooms.delete(connection.roomId);
            console.log(`[INFO] 删除空房间: ${connection.roomId}`);
        } else if (room.connections.length === 1) {
            // 通知剩余客户端对端已离线
            const remainingClient = room.connections[0];
            this.sendError(remainingClient, ERROR_CODES.PEER_OFFLINE, "对端已离线");
            
            // 重置房间状态为等待
            room.status = ROOM_STATUS.WAITING;
            remainingClient.status = CONNECTION_STATUS.JOINED;
            delete remainingClient.role;
            
            // 发送房间信息变动通知（房间状态变为等待）
            this.sendRoomInfo(room);
            
            // 重新设置房间超时
            room.timeout = setTimeout(() => {
                this.handleRoomTimeout(connection.roomId);
            }, CONFIG.ROOM_TIMEOUT);
        }

        connection.roomId = null;
        connection.status = CONNECTION_STATUS.CONNECTED;
        delete connection.role;
    }

    // 处理房间超时
    handleRoomTimeout(roomId) {
        const room = this.rooms.get(roomId);
        if (!room) return;

        console.log(`[INFO] 房间 ${roomId} 超时，踢出所有客户端`);

        // 发送房间关闭信息变动通知
        room.status = ROOM_STATUS.TIMEOUT;
        this.sendRoomInfo(room);

        // 通知所有客户端房间超时
        room.connections.forEach(connection => {
            this.sendError(connection, ERROR_CODES.CONNECTION_TIMEOUT, "房间超时");
            connection.roomId = null;
            connection.status = CONNECTION_STATUS.CONNECTED;
            delete connection.role;
        });

        // 删除房间
        this.rooms.delete(roomId);
    }

    // 转发消息给对端
    forwardMessage(fromConnection, message) {
        if (!fromConnection.roomId) {
            return this.sendError(fromConnection, ERROR_CODES.ROOM_NOT_EXISTS, "未加入房间");
        }

        const room = this.rooms.get(fromConnection.roomId);
        if (!room || room.status !== ROOM_STATUS.PAIRED) {
            return this.sendError(fromConnection, ERROR_CODES.PEER_OFFLINE, "对端未连接");
        }

        // 找到对端连接
        const peerConnection = room.connections.find(conn => conn !== fromConnection);
        if (!peerConnection) {
            return this.sendError(fromConnection, ERROR_CODES.PEER_OFFLINE, "对端已离线");
        }

        // 添加时间戳并转发
        message.time = this.getCurrentTimestamp();
        this.sendMessage(peerConnection, message);
        
        console.log(`[INFO] 转发消息: ${message.type} from ${fromConnection.deviceId} to ${peerConnection.deviceId}`);
    }

    // 提取房间ID（从设备ID中提取数字部分）
    extractRoomId(deviceId) {
        const match = deviceId.match(/(\d+)$/);
        return match ? match[1] : 'default';
    }

    // 发送错误消息
    sendError(connection, errorCode, errorMessage) {
        const message = {
            type: 'error',
            device_id: connection.deviceId,
            from: 'server',
            to: connection.deviceId,
            data: {
                error_code: errorCode,
                error_message: errorMessage
            },
            time: this.getCurrentTimestamp()
        };
        
        this.sendMessage(connection, message);
        console.log(`[ERROR] 发送错误给 ${connection.deviceId}: ${errorCode} - ${errorMessage}`);
    }

    // 发送消息
    sendMessage(connection, message) {
        if (connection.ws.readyState === WebSocket.OPEN) {
            connection.ws.send(JSON.stringify(message));
        }
    }

    // 获取当前微秒时间戳
    getCurrentTimestamp() {
        return Date.now() * 1000 + Math.floor(Math.random() * 1000);
    }

    // 获取房间状态（用于监控）
    getRoomStats() {
        return {
            totalRooms: this.rooms.size,
            totalConnections: this.connections.size,
            rooms: Array.from(this.rooms.values()).map(room => ({
                id: room.id,
                status: room.status,
                connectionCount: room.connections.length,
                createdAt: room.createdAt
            }))
        };
    }

    // 基于特定事件下发房间信息
    sendRoomInfoByEvent(roomId, eventType, eventData = {}) {
        const room = this.rooms.get(roomId);
        if (!room || room.connections.length === 0) return;

        console.log(`[INFO] 基于事件 ${eventType} 下发房间信息: ${roomId}`);

        const roomInfo = {
            room_id: room.id,
            num: room.connections.length,
            room_status: room.status === ROOM_STATUS.PAIRED ? 'open' : 'close',
            event_type: eventType,
            event_data: eventData,
            timestamp: this.getCurrentTimestamp()
        };

        // 向房间内所有客户端发送
        room.connections.forEach(connection => {
            const message = {
                type: 'info',
                device_id: connection.deviceId,
                from: 'server',
                to: connection.deviceId,
                data: roomInfo,
                time: this.getCurrentTimestamp()
            };
            
            this.sendMessage(connection, message);
            console.log(`[INFO] 发送事件房间信息给 ${connection.deviceId}: 事件${eventType}, 房间${room.id}, 人数${roomInfo.num}, 状态${roomInfo.room_status}`);
        });
    }

    // 客户端请求房间信息
    sendRoomInfoOnRequest(connection) {
        if (!connection.roomId) {
            return this.sendError(connection, ERROR_CODES.ROOM_NOT_EXISTS, "未加入房间");
        }

        const room = this.rooms.get(connection.roomId);
        if (!room) {
            return this.sendError(connection, ERROR_CODES.ROOM_NOT_EXISTS, "房间不存在");
        }

        console.log(`[INFO] 客户端 ${connection.deviceId} 请求房间信息: ${connection.roomId}`);

        const roomInfo = {
            room_id: room.id,
            num: room.connections.length,
            room_status: room.status === ROOM_STATUS.PAIRED ? 'open' : 'close',
            request_type: 'client_request',
            timestamp: this.getCurrentTimestamp()
        };

        const message = {
            type: 'info',
            device_id: connection.deviceId,
            from: 'server',
            to: connection.deviceId,
            data: roomInfo,
            time: this.getCurrentTimestamp()
        };
        
        this.sendMessage(connection, message);
        console.log(`[INFO] 发送请求房间信息给 ${connection.deviceId}: 房间${room.id}, 人数${roomInfo.num}, 状态${roomInfo.room_status}`);
    }

    // 广播房间信息给指定房间
    broadcastRoomInfo(roomId, customData = {}) {
        const room = this.rooms.get(roomId);
        if (!room || room.connections.length === 0) return;

        console.log(`[INFO] 广播房间信息: ${roomId}`);

        const roomInfo = {
            room_id: room.id,
            num: room.connections.length,
            room_status: room.status === ROOM_STATUS.PAIRED ? 'open' : 'close',
            broadcast_type: 'manual',
            ...customData,
            timestamp: this.getCurrentTimestamp()
        };

        // 向房间内所有客户端发送
        room.connections.forEach(connection => {
            const message = {
                type: 'info',
                device_id: connection.deviceId,
                from: 'server',
                to: connection.deviceId,
                data: roomInfo,
                time: this.getCurrentTimestamp()
            };
            
            this.sendMessage(connection, message);
            console.log(`[INFO] 广播房间信息给 ${connection.deviceId}: 房间${room.id}, 人数${roomInfo.num}, 状态${roomInfo.room_status}`);
        });
    }
}

// 创建HTTP服务器和WebSocket服务器
const server = http.createServer();
const wss = new WebSocket.Server({ server });
const roomManager = new RoomManager();

// 连接计数器
let connectionCounter = 0;

// WebSocket连接处理
wss.on('connection', (ws, req) => {
    const connectionId = `conn_${++connectionCounter}`;
    
    // 创建连接对象
    const connection = {
        id: connectionId,
        ws: ws,
        deviceId: null,
        roomId: null,
        status: CONNECTION_STATUS.CONNECTED,
        role: null,
        connectedAt: Date.now()
    };

    roomManager.connections.set(connectionId, connection);
    console.log(`[INFO] 新客户端连接: ${connectionId}, 总连接数: ${roomManager.connections.size}`);

    // 消息处理
    ws.on('message', (data) => {
        try {
            const message = JSON.parse(data.toString('utf8'));
            
            // 验证消息格式
            if (!message.type || !message.device_id || !message.from || !message.to) {
                return roomManager.sendError(connection, ERROR_CODES.MESSAGE_FORMAT_ERROR, "消息格式错误");
            }

            // 设置连接的设备ID
            if (!connection.deviceId) {
                connection.deviceId = message.device_id;
            }

            console.log(`[INFO] 收到消息: ${message.type} from ${message.from}`);

            // 根据消息类型处理
            switch (message.type) {
                case 'join':
                    roomManager.joinRoom(connection, message.device_id);
                    break;
                    
                case 'leave':
                    roomManager.leaveRoom(connection);
                    break;
                    
                case 'offer':
                case 'answer':
                case 'ice':
                    roomManager.forwardMessage(connection, message);
                    break;
                    
                case 'get_room_info':
                    roomManager.sendRoomInfoOnRequest(connection);
                    break;
                    
                case 'broadcast_room_info':
                    // 处理广播房间信息请求（需要管理员权限或特殊验证）
                    if (message.data && message.data.room_id) {
                        roomManager.broadcastRoomInfo(message.data.room_id, message.data.custom_data || {});
                    } else {
                        roomManager.sendError(connection, ERROR_CODES.MESSAGE_FORMAT_ERROR, "广播房间信息需要指定room_id");
                    }
                    break;
                    
                case 'trigger_event':
                    // 处理触发特定事件请求
                    if (message.data && message.data.room_id && message.data.event_type) {
                        eventManager.triggerCustomEvent(
                            message.data.room_id, 
                            message.data.event_type, 
                            message.data.event_data || {}
                        );
                    } else {
                        roomManager.sendError(connection, ERROR_CODES.MESSAGE_FORMAT_ERROR, "触发事件需要指定room_id和event_type");
                    }
                    break;
                    
                default:
                    roomManager.sendError(connection, ERROR_CODES.MESSAGE_FORMAT_ERROR, `未知消息类型: ${message.type}`);
                    break;
            }
            
        } catch (error) {
            console.error(`[ERROR] 解析消息失败: ${error.message}`);
            roomManager.sendError(connection, ERROR_CODES.MESSAGE_FORMAT_ERROR, "JSON解析失败");
        }
    });

    // 连接关闭处理
    ws.on('close', () => {
        console.log(`[INFO] 客户端断开: ${connectionId} (${connection.deviceId || 'unknown'})`);
        
        // 离开房间
        roomManager.leaveRoom(connection);
        
        // 移除连接
        roomManager.connections.delete(connectionId);
        
        console.log(`[INFO] 总连接数: ${roomManager.connections.size}`);
    });

    // 错误处理
    ws.on('error', (error) => {
        console.error(`[ERROR] WebSocket错误: ${error.message}`);
    });
});

// 启动服务器
server.listen(CONFIG.PORT, () => {
    console.log(`[INFO] WebSocket信令服务器启动成功`);
    console.log(`[INFO] 监听端口: ${CONFIG.PORT}`);
    console.log(`[INFO] 房间超时时间: ${CONFIG.ROOM_TIMEOUT}ms`);
    console.log(`[INFO] 最大连接数: ${CONFIG.MAX_CONNECTIONS}`);
});

// 定期输出服务器状态
setInterval(() => {
    const stats = roomManager.getRoomStats();
    console.log(`[STATS] 房间数: ${stats.totalRooms}, 连接数: ${stats.totalConnections}`);
}, 30000); // 每30秒输出一次

// 基于特定事件的房间信息下发机制
class EventManager {
    constructor(roomManager) {
        this.roomManager = roomManager;
        this.eventListeners = new Map();
        this.setupEventHandlers();
    }

    // 设置事件处理器
    setupEventHandlers() {
        // 定期房间状态检查事件
        setInterval(() => {
            this.triggerPeriodicRoomCheck();
        }, 60000); // 每分钟检查一次

        // WebRTC连接状态变化事件（模拟）
        setInterval(() => {
            this.triggerWebRTCStatusCheck();
        }, 15000); // 每15秒检查一次WebRTC状态

        // 网络质量检查事件
        setInterval(() => {
            this.triggerNetworkQualityCheck();
        }, 30000); // 每30秒检查一次网络质量
    }

    // 定期房间状态检查
    triggerPeriodicRoomCheck() {
        console.log(`[EVENT] 触发定期房间状态检查`);
        
        this.roomManager.rooms.forEach((room, roomId) => {
            if (room.connections.length > 0) {
                this.roomManager.sendRoomInfoByEvent(roomId, 'periodic_check', {
                    check_type: 'room_status',
                    room_age: Date.now() - room.createdAt,
                    last_check: Date.now()
                });
            }
        });
    }

    // WebRTC连接状态检查
    triggerWebRTCStatusCheck() {
        console.log(`[EVENT] 触发WebRTC连接状态检查`);
        
        this.roomManager.rooms.forEach((room, roomId) => {
            if (room.status === ROOM_STATUS.PAIRED && room.connections.length === 2) {
                // 模拟WebRTC连接状态检查
                const connectionQuality = this.simulateConnectionQuality();
                
                this.roomManager.sendRoomInfoByEvent(roomId, 'webrtc_status_check', {
                    connection_quality: connectionQuality,
                    ice_state: 'connected',
                    data_channel_open: true,
                    last_check: Date.now()
                });
            }
        });
    }

    // 网络质量检查
    triggerNetworkQualityCheck() {
        console.log(`[EVENT] 触发网络质量检查`);
        
        this.roomManager.rooms.forEach((room, roomId) => {
            if (room.connections.length > 0) {
                const networkQuality = this.simulateNetworkQuality();
                
                this.roomManager.sendRoomInfoByEvent(roomId, 'network_quality_check', {
                    latency: networkQuality.latency,
                    bandwidth: networkQuality.bandwidth,
                    packet_loss: networkQuality.packetLoss,
                    last_check: Date.now()
                });
            }
        });
    }

    // 模拟连接质量
    simulateConnectionQuality() {
        const qualities = ['excellent', 'good', 'fair', 'poor'];
        return qualities[Math.floor(Math.random() * qualities.length)];
    }

    // 模拟网络质量
    simulateNetworkQuality() {
        return {
            latency: Math.floor(Math.random() * 100) + 10, // 10-110ms
            bandwidth: Math.floor(Math.random() * 5000) + 1000, // 1-6 Mbps
            packetLoss: Math.random() * 0.05 // 0-5% packet loss
        };
    }

    // 手动触发特定事件
    triggerCustomEvent(roomId, eventType, eventData) {
        if (this.roomManager.rooms.has(roomId)) {
            this.roomManager.sendRoomInfoByEvent(roomId, eventType, eventData);
            console.log(`[EVENT] 手动触发事件: ${eventType} for room: ${roomId}`);
        } else {
            console.log(`[EVENT] 房间不存在: ${roomId}`);
        }
    }

    // 添加事件监听器
    addEventListener(eventType, callback) {
        if (!this.eventListeners.has(eventType)) {
            this.eventListeners.set(eventType, []);
        }
        this.eventListeners.get(eventType).push(callback);
    }

    // 移除事件监听器
    removeEventListener(eventType, callback) {
        if (this.eventListeners.has(eventType)) {
            const listeners = this.eventListeners.get(eventType);
            const index = listeners.indexOf(callback);
            if (index > -1) {
                listeners.splice(index, 1);
            }
        }
    }
}

// 创建事件管理器
const eventManager = new EventManager(roomManager);

// 优雅关闭处理
process.on('SIGINT', () => {
    console.log('\n[INFO] 正在关闭服务器...');
    
    // 关闭所有WebSocket连接
    wss.clients.forEach(ws => {
        if (ws.readyState === WebSocket.OPEN) {
            ws.close();
        }
    });
    
    server.close(() => {
        console.log('[INFO] 服务器已关闭');
        process.exit(0);
    });
});

module.exports = { roomManager, eventManager, wss, server };
