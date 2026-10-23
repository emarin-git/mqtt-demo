"""Smart object MCP server.

Bridges an ESP32 smart object over MQTT with an MCP server:
- Subscribes to the counter topic and keeps the last received value.
- Exposes MCP tools to turn the internal LED on/off and to read the counter.
"""

import logging
import sys
import threading

import paho.mqtt.client as mqtt
from mcp.server.mcpserver import MCPServer

MQTT_BROKER = "broker.hivemq.com"
MQTT_PORT = 1883
MQTT_KEEPALIVE = 60
TOPIC_COUNTER = "iot/mqtt-demo/esp32/contador"
TOPIC_LED = "iot/mqtt-demo/esp32/led"

# Logs go to stderr: with the stdio transport, stdout carries the MCP protocol.
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s [%(name)s] %(message)s",
    stream=sys.stderr,
)
logger = logging.getLogger("smart-object")

# Last value received on the counter topic (None until the first message).
_counter: str | None = None
_counter_lock = threading.Lock()


def on_connect(client, userdata, flags, reason_code, properties):
    if reason_code.is_failure:
        logger.error("Failed to connect to MQTT broker: %s", reason_code)
        return
    logger.info("Connected to MQTT broker %s:%d", MQTT_BROKER, MQTT_PORT)
    # Subscribing here re-subscribes automatically after a reconnect.
    client.subscribe(TOPIC_COUNTER)
    logger.info("Subscribed to topic %s", TOPIC_COUNTER)


def on_disconnect(client, userdata, flags, reason_code, properties):
    logger.warning("Disconnected from MQTT broker: %s (will retry)", reason_code)


def on_message(client, userdata, msg):
    global _counter
    value = msg.payload.decode("utf-8", errors="replace").strip()
    with _counter_lock:
        _counter = value
    logger.info("Counter updated from %s: %s", msg.topic, value)


mqtt_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
mqtt_client.on_connect = on_connect
mqtt_client.on_disconnect = on_disconnect
mqtt_client.on_message = on_message
mqtt_client.reconnect_delay_set(min_delay=1, max_delay=30)

mcp = MCPServer("smart-object")


def _publish_led(state: str) -> str:
    info = mqtt_client.publish(TOPIC_LED, state)
    if info.rc != mqtt.MQTT_ERR_SUCCESS:
        logger.error("Failed to publish %s to %s: %s", state, TOPIC_LED, mqtt.error_string(info.rc))
        return f"Error: could not send {state} to the LED ({mqtt.error_string(info.rc)})"
    logger.info("Published %s to %s", state, TOPIC_LED)
    return f"LED command {state} sent"


@mcp.tool()
def turn_on_led() -> str:
    """Turn ON the internal LED of the smart object."""
    return _publish_led("ON")


@mcp.tool()
def turn_off_led() -> str:
    """Turn OFF the internal LED of the smart object."""
    return _publish_led("OFF")


@mcp.tool()
def get_counter() -> str:
    """Get the latest counter value reported by the smart object."""
    with _counter_lock:
        value = _counter
    if value is None:
        logger.info("Counter requested but no value received yet")
        return "No counter value received yet"
    logger.info("Counter requested: %s", value)
    return value


def main() -> None:
    logger.info("Connecting to MQTT broker %s:%d", MQTT_BROKER, MQTT_PORT)
    # connect_async + loop_start: the network loop runs in a background thread
    # and keeps retrying, even if the broker is unreachable at startup.
    mqtt_client.connect_async(MQTT_BROKER, MQTT_PORT, MQTT_KEEPALIVE)
    mqtt_client.loop_start()
    try:
        logger.info("Starting MCP server")
        mcp.run()  # blocks until the MCP client closes the connection
    except KeyboardInterrupt:
        logger.info("Interrupted, shutting down")
    finally:
        mqtt_client.loop_stop()
        mqtt_client.disconnect()
        logger.info("MQTT client stopped")


if __name__ == "__main__":
    main()
