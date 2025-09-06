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

module.exports = { roomManager, wss, server };
