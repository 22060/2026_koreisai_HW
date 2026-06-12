const WebSocket = require('ws');

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

    console.log(`Controller Removed : ${id}`);
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
    console.log("Sensor Connected");

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

            controllers[controllerId] =
            {
                ...data,
                lastUpdate: now()
            };

            sensorSockets.set(controllerId, ws);
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

        console.log("Sensor Disconnected");
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
    console.log("Game Connected");

    gameClients.add(ws);

    ws.send(JSON.stringify({
        type: "connected",
        serverTime: now()
    }));

    ws.on('close', () =>
    {
        gameClients.delete(ws);

        console.log("Game Disconnected");
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

                    console.log(
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

    broadcastToGames(packet);

}, 1000 / GAME_UPDATE_RATE);

//==================================================
// Console Status
//==================================================

setInterval(() =>
{
    console.clear();

    console.log("================================");
    console.log(" Controller Server");
    console.log("================================");

    console.log(`Sensor Port : ${SENSOR_PORT}`);
    console.log(`Game Port   : ${GAME_PORT}`);

    console.log("");

    console.log(
        `Game Clients : ${gameClients.size}`
    );

    console.log(
        `Controllers  : ${Object.keys(controllers).length}`
    );

    console.log("");

    Object.values(controllers).forEach(c =>
    {
        console.log(
            `${c.id}`
        );
    });

}, 1000);

console.log("================================");
console.log("Controller Server Started");
console.log(`Sensor WS : ${SENSOR_PORT}`);
console.log(`Game WS   : ${GAME_PORT}`);
console.log("================================");