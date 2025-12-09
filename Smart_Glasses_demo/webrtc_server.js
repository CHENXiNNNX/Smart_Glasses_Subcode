const WebSocket = require('ws');
const http = require('http');
const os = require('os');

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

    // 提取房间ID（从设备ID中提取数字部分）
    extractRoomId(deviceId) {
        // 支持格式：glasses_123456 或 app_123456
        const match = deviceId.match(/(\d+)$/);
        return match ? `room_${match[1]}` : 'room_default';
    }

    // 判断设备类型
    isDevice(deviceId) {
        return deviceId.startsWith('glasses_');
    }

    isApp(deviceId) {
        return deviceId.startsWith('app_');
    }

    // 验证设备ID格式
    validateDeviceId(deviceId) {
        return this.isDevice(deviceId) || this.isApp(deviceId);
    }

    // 创建或加入房间
    joinRoom(connection, deviceId) {
        // 验证设备ID格式
        if (!this.validateDeviceId(deviceId)) {
            return this.sendError(connection, ERROR_CODES.DEVICE_ID_ERROR, "设备ID格式错误，应为glasses_xxx或app_xxx");
        }

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
        connection.deviceId = deviceId;
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
        
        // 根据新方案：设备端始终作为offerer，APP端始终作为answerer
        // 无论谁先加入，角色都是固定的
        let deviceConnection, appConnection;
        if (this.isDevice(client1.deviceId)) {
            deviceConnection = client1;
            appConnection = client2;
        } else {
            deviceConnection = client2;
            appConnection = client1;
        }
        
        deviceConnection.role = 'offerer';
        appConnection.role = 'answerer';
        
        deviceConnection.status = CONNECTION_STATUS.PAIRED;
        appConnection.status = CONNECTION_STATUS.PAIRED;

        // 发送角色信息给双方
        // 设备端收到对端APP的ID
        this.sendRoleMessage(deviceConnection, appConnection.deviceId);
        // APP端收到对端设备的ID
        this.sendRoleMessage(appConnection, deviceConnection.deviceId);

        // 发送配对后的房间信息变动通知
        this.sendRoomInfo(room);

        console.log(`[INFO] 房间 ${room.id} 配对成功: ${deviceConnection.deviceId}(offerer) <-> ${appConnection.deviceId}(answerer)`);
    }

    // 发送角色信息
    sendRoleMessage(connection, peerDeviceId) {
        const message = {
            type: 'role',
            from: 'server',
            to: connection.deviceId,
            data: {
                peer_id: peerDeviceId
            },
            time: this.getCurrentTimestamp()
        };
        
        this.sendMessage(connection, message);
        console.log(`[INFO] 发送角色信息给 ${connection.deviceId}: 对端=${peerDeviceId}, 角色=${connection.role}`);
    }

    // 发送房间信息变动消息
    sendRoomInfo(room) {
        if (!room || room.connections.length === 0) return;

        const roomInfo = {
            room_id: room.id,
            num: room.connections.length
        };

        // 向房间内所有客户端发送房间信息
        room.connections.forEach(connection => {
            const message = {
                type: 'info',
                from: 'server',
                to: connection.deviceId,
                data: roomInfo,
                time: this.getCurrentTimestamp()
            };
            
            this.sendMessage(connection, message);
            console.log(`[INFO] 发送房间信息给 ${connection.deviceId}: 房间=${room.id}, 人数=${roomInfo.num}`);
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
        if (!room) {
            return this.sendError(fromConnection, ERROR_CODES.ROOM_NOT_EXISTS, "房间不存在");
        }

        // get_connect和respond_post消息可以在未配对时转发（用于发起连接请求和回应）
        if (message.type === 'get_connect' || message.type === 'respond_post') {
            if (room.connections.length < 2) {
                return this.sendError(fromConnection, ERROR_CODES.PEER_OFFLINE, "对端未加入房间");
            }
        } else {
            // 其他消息（offer, answer, ice）需要在配对后转发
            if (room.status !== ROOM_STATUS.PAIRED) {
                return this.sendError(fromConnection, ERROR_CODES.PEER_OFFLINE, "对端未连接");
            }
        }

        // 找到对端连接
        const peerConnection = room.connections.find(conn => conn !== fromConnection);
        if (!peerConnection) {
            return this.sendError(fromConnection, ERROR_CODES.PEER_OFFLINE, "对端已离线");
        }

        // 打印SDP和ICE内容（用于调试）
        if (message.type === 'offer' || message.type === 'answer') {
            if (message.data && message.data.sdp) {
                console.log(`\n[SDP] ========== ${message.type.toUpperCase()} SDP ==========`);
                console.log(`[SDP] 发送方: ${fromConnection.deviceId} (${fromConnection.role || 'unknown'})`);
                console.log(`[SDP] 接收方: ${peerConnection.deviceId} (${peerConnection.role || 'unknown'})`);
                console.log(`[SDP] 房间ID: ${room.id}`);
                if (typeof message.data.sdp === 'string') {
                    console.log(`[SDP] SDP内容:`);
                    const lines = message.data.sdp.split('\n');
                    lines.forEach(line => {
                        if (line.trim()) {
                            console.log(`[SDP]   ${line}`);
                        }
                    });
                } else {
                    console.log(`[SDP] SDP数据:`, JSON.stringify(message.data.sdp, null, 2));
                }
                console.log(`[SDP] ===========================================\n`);
            }
        } else if (message.type === 'ice') {
            if (message.data && message.data.candidate) {
                console.log(`[ICE] 转发ICE候选: ${fromConnection.deviceId} -> ${peerConnection.deviceId}`);
                if (typeof message.data.candidate === 'string') {
                    console.log(`[ICE]   候选: ${message.data.candidate.substring(0, 100)}...`);
                }
            }
        }

        // 添加时间戳并转发
        message.time = this.getCurrentTimestamp();
        this.sendMessage(peerConnection, message);
        
        console.log(`[INFO] 转发消息: ${message.type} from ${fromConnection.deviceId} to ${peerConnection.deviceId}`);
    }

    // 发送错误消息
    sendError(connection, errorCode, errorMessage) {
        const message = {
            type: 'error',
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
        if (connection.ws && connection.ws.readyState === WebSocket.OPEN) {
            try {
                const jsonMessage = JSON.stringify(message);
                connection.ws.send(jsonMessage);
            } catch (error) {
                console.error(`[ERROR] 发送消息失败: ${error.message}`);
            }
        }
    }

    // 获取当前微秒时间戳
    getCurrentTimestamp() {
        return Date.now() * 1000; // 转换为微秒
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
                createdAt: room.createdAt,
                devices: room.connections.map(conn => ({
                    deviceId: conn.deviceId,
                    role: conn.role || 'none',
                    status: conn.status
                }))
            }))
        };
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
            num: room.connections.length
        };

        const message = {
            type: 'info',
            from: 'server',
            to: connection.deviceId,
            data: roomInfo,
            time: this.getCurrentTimestamp()
        };
        
        this.sendMessage(connection, message);
        console.log(`[INFO] 发送请求房间信息给 ${connection.deviceId}: 房间=${room.id}, 人数=${roomInfo.num}`);
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
            
            // 验证消息格式（根据新方案，消息应包含type, from, to, time）
            if (!message.type || !message.from || !message.to) {
                return roomManager.sendError(connection, ERROR_CODES.MESSAGE_FORMAT_ERROR, "消息格式错误：缺少必需字段(type, from, to)");
            }

            // 设置连接的设备ID（从from字段获取）
            if (!connection.deviceId) {
                connection.deviceId = message.from;
            }

            // 验证设备ID格式
            if (!roomManager.validateDeviceId(connection.deviceId)) {
                return roomManager.sendError(connection, ERROR_CODES.DEVICE_ID_ERROR, "设备ID格式错误，应为glasses_xxx或app_xxx");
            }

            console.log(`[INFO] 收到消息: type=${message.type}, from=${message.from}, to=${message.to}`);

            // 根据消息类型处理
            switch (message.type) {
                case 'join':
                    roomManager.joinRoom(connection, message.from);
                    break;
                    
                case 'leave':
                    roomManager.leaveRoom(connection);
                    break;
                    
                case 'offer':
                case 'answer':
                case 'ice':
                    // 转发SDP和ICE消息给对端
                    roomManager.forwardMessage(connection, message);
                    break;
                    
                case 'get_connect':
                    // 转发连接请求给对端
                    roomManager.forwardMessage(connection, message);
                    break;
                    
                case 'respond_post':
                    // 转发连接请求回应给对端
                    roomManager.forwardMessage(connection, message);
                    break;
                    
                case 'get_room_info':
                    roomManager.sendRoomInfoOnRequest(connection);
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

// 获取本地网络IP地址
function getLocalIPAddresses() {
    const interfaces = os.networkInterfaces();
    const addresses = [];
    
    for (const interfaceName in interfaces) {
        const iface = interfaces[interfaceName];
        for (const alias of iface) {
            // 跳过内部（即127.0.0.1）和非IPv4地址
            if (alias.family === 'IPv4' && !alias.internal) {
                addresses.push({
                    interface: interfaceName,
                    address: alias.address,
                    netmask: alias.netmask
                });
            }
        }
    }
    
    return addresses;
}

// 启动服务器
server.listen(CONFIG.PORT, () => {
    console.log(`\n[INFO] ==========================================`);
    console.log(`[INFO] WebSocket信令服务器启动成功`);
    console.log(`[INFO] ==========================================`);
    console.log(`[INFO] 监听端口: ${CONFIG.PORT}`);
    console.log(`[INFO] 房间超时时间: ${CONFIG.ROOM_TIMEOUT}ms`);
    console.log(`[INFO] 最大连接数: ${CONFIG.MAX_CONNECTIONS}`);
    
    // 打印本地网络IP地址
    const ipAddresses = getLocalIPAddresses();
    if (ipAddresses.length > 0) {
        console.log(`[INFO] 本地网络IP地址:`);
        ipAddresses.forEach((ip, index) => {
            console.log(`[INFO]   ${index + 1}. ${ip.interface}: ${ip.address} (${ip.netmask})`);
        });
    } else {
        console.log(`[WARN] 未找到可用的网络接口`);
    }
    
    // 打印本地回环地址
    console.log(`[INFO] 本地回环地址: 127.0.0.1`);
    console.log(`[INFO] ==========================================\n`);
});

// 定期房间状态检查，每60秒自动下发房间信息（符合文档要求）
setInterval(() => {
    console.log(`[INFO] 定期房间状态检查，下发房间信息`);
    roomManager.rooms.forEach((room, roomId) => {
        if (room.connections.length > 0) {
            roomManager.sendRoomInfo(room);
        }
    });
}, 60000); // 每60秒检查一次

// 定期输出服务器状态
setInterval(() => {
    const stats = roomManager.getRoomStats();
    console.log(`[STATS] 房间数: ${stats.totalRooms}, 连接数: ${stats.totalConnections}`);
    if (stats.rooms.length > 0) {
        stats.rooms.forEach(room => {
            console.log(`[STATS]   房间 ${room.id}: 状态=${room.status}, 人数=${room.connectionCount}`);
        });
    }
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

