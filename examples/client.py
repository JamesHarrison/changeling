# A really simple example client for watching what changeling's doing.
import time
import mosquitto
client = mosquitto.Mosquitto("changeling-example-client")
client.connect("localhost")
client.subscribe("changeling-status",0)
def on_message(obj, msg):
    print("Message received on topic "+msg.topic+" with QoS "+str(msg.qos)+" and payload "+msg.payload)

client.on_message = on_message
while True:
    client.loop()
    time.sleep(0.05)

