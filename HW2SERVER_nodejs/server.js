const WebSocket = require('ws');
const http = require('http');
const express = require('express');
const path = require('path');
const MONITOR_HTTP_PORT = 8085;
const app = express();

app.use(
    express.static(
        path.join(__dirname, "public")
    )
);

app.listen(
    MONITOR_HTTP_PORT,
    () =>
    {
        console.log(
            `Monitor HTTP : ${MONITOR_HTTP_PORT}`
        );
    }
);
const monitorServer =
    new WebSocket.Server({
        port: 8083
    });

const monitorClients =
    new Set();

monitorServer.on(
    "connection",
    ws =>
{
    monitorClients.add(ws);

    ws.on(
        "close",
        () =>
        {
            monitorClients.delete(ws);
        }
    );
});
function webLog(message)
{
    console.log(message);

    const packet =
    {
        type:"log",
        time:new Date().toLocaleTimeString(),
        message
    };

    const json =
        JSON.stringify(packet);

    monitorClients.forEach(client =>
    {
        if(client.readyState === WebSocket.OPEN)
        {
            client.send(json);
        }
    });
}
//==================================================
// Settings
//==================================================

const SENSOR_PORT = 8081;
const GAME_PORT = 8082;

const SENSOR_TIMEOUT_MS = 3000;
const GAME_UPDATE_RATE = 60;

//==================================================
// Servers
//==================================================

const sensorServer = new WebSocket.Server({
    port: SENSOR_PORT
});

const gameServer = new WebSocket.Server({
    port: GAME_PORT
});

//==================================================
// State
//==================================================

const controllers = {};
const sensorSockets = new Map();

const gameClients = new Set();

//==================================================
// Utility
//==================================================

function now()
{
    return Date.now();
}

function removeController(id)
{
    delete controllers[id];
    sensorSockets.delete(id);

    webLog(`Controller Removed : ${id}`);
}

function broadcastToGames(packet)
{
    const message = JSON.stringify(packet);

    gameClients.forEach(client =>
    {
        if(client.readyState === WebSocket.OPEN)
        {
            client.send(message);
        }
    });
}

//==================================================
// Sensor Connection
//==================================================

sensorServer.on('connection', ws =>
{
    webLog("Sensor Connected");

    let controllerId = null;

    ws.on('message', message =>
    {
        try
        {
            const data = JSON.parse(message.toString());

            if(!data.id)
            {
                return;
            }

            controllerId = data.id;

            const receivedAt = now();
            const transportDelay =
                receivedAt - data.timestamp;

            controllers[controllerId] =
            {
                ...data,
                lastUpdate: receivedAt,
                receivedAt,
                transportDelay
            };

            sensorSockets.set(controllerId, ws);

            console.log(
                `[${controllerId}] ` +
                `transport=${transportDelay}ms`
            );
        }
        catch(error)
        {
            console.error("Sensor Parse Error");
            console.error(error);
        }
    });

    ws.on('close', () =>
    {
        if(controllerId)
        {
            removeController(controllerId);
        }

        webLog("Sensor Disconnected");
    });

    ws.on('error', error =>
    {
        console.error(error);
    });
});

//==================================================
// Game Connection
//==================================================

gameServer.on('connection', ws =>
{
    webLog("Game Connected");

    gameClients.add(ws);

    ws.send(JSON.stringify({
        type: "connected",
        serverTime: now()
    }));

    ws.on('close', () =>
    {
        gameClients.delete(ws);

        webLog("Game Disconnected");
    });

    ws.on('error', error =>
    {
        console.error(error);
    });
     ws.on('message', message =>
    {
        try
        {
            const packet =
                JSON.parse(message);

            if(packet.type === "calibrate")
            {
                const sensor =
                    sensorSockets.get(
                        packet.target
                    );

                if(sensor)
                {
                    sensor.send(
                        JSON.stringify({
                            type:"calibrate"
                        })
                    );

                    webLog(
                        `Calibration request -> ${packet.target}`
                    );
                }
            }
        }
        catch(err)
        {
            console.error(err);
        }
    });
});

//==================================================
// Sensor Timeout Check
//==================================================

setInterval(() =>
{
    const current = now();

    Object.keys(controllers).forEach(id =>
    {
        const age = current - controllers[id].lastUpdate;

        if(age > SENSOR_TIMEOUT_MS)
        {
            removeController(id);
        }
    });

}, 1000);

//==================================================
// Game Update Loop
//==================================================

setInterval(() =>
{
    const packet =
    {
        type: "state",

        timestamp: now(),

        controllerCount:
            Object.keys(controllers).length,

        controllers
    };
    for(const id in controllers)
    {
        if(controllers[id].ping > 400){
            webLog(`Controller ${id} ping is high: ${controllers[id].ping}ms`);
        }
    }

    broadcastToGames(packet);

}, 1000 / GAME_UPDATE_RATE);

//==================================================
// Console Status
//==================================================

setInterval(() =>
{
    const packet =
    {
        type: "status",

        timestamp: now(),

        sensorPort: SENSOR_PORT,
        gamePort: GAME_PORT,

        gameClients: gameClients.size,

        controllerCount:
            Object.keys(controllers).length,

        controllers: controllers
    };

    const json =
        JSON.stringify(packet);

    for(const client of monitorClients)
    {
        if(client.readyState === WebSocket.OPEN)
        {
            client.send(json);
        }
    }

}, 1000);

webLog("================================");
webLog("Controller Server Started");
webLog(`Sensor WS : ${SENSOR_PORT}`);
webLog(`Game WS   : ${GAME_PORT}`);
webLog("================================");