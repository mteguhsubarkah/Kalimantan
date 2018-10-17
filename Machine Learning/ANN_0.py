import time
import paho.mqtt.client as paho
import random
import math

import matplotlib
from pylab import plot, legend, subplot, grid, xlabel, ylabel, show, title

from pyneurgen.neuralnet import NeuralNet
from pyneurgen.nodes import BiasNode, Connection

broker="123.45.0.10"
port = 1883
WindSpeed = ""
WindDirection = ""
VesselHeading = ""
Surge_Init = 0
Sway_Init = 0
Yaw_Init = 0
start_time = time.time()


def on_message(client, userdata, message):
	topic = str(message.topic)
	print(topic)
	if (topic == "WindDirection"):
		WindDirection = float(str(message.payload.decode("utf-8")))
		print("Wind Direction =",WindDirection)
	if (topic == "WindSpeed"):
		WindSpeed = float(str(message.payload.decode("utf-8")))
		print("Wind Speed =",WindSpeed)
	if (topic == "VesselHeading"):
		VesselHeading = float(str(message.payload.decode("utf-8")))
		print("Vessel Heading =",str(message.payload.decode("utf-8")))
    

def on_publish(client,userdata,result):             #create function for callback
    #print("data published \n")
    pass

client= paho.Client("client-001") #create client object client1.on_publish = on_publish #assign function to callback client1.connect(broker,port) #establish connection client1.publish("house/bulb1","on")
client.on_publish = on_publish                          #assign function to callback
######Bind function to callback
client.on_message=on_message


print("connecting to broker ",broker)
client.connect(broker,port)#connect

client.loop_start() #start loop to process received messages

print("Vessel Initial Position = %f;%f;%f"%(Surge_Init,Sway_Init,Yaw_Init))

while True:

	client.loop_start()
	client.publish("NN_active","1")
	client.publish("spc_speed1","1300")
	client.subscribe("WindSpeed")
	client.subscribe("WindDirection")
	client.subscribe("VesselHeading")
	
	client.loop_stop()
