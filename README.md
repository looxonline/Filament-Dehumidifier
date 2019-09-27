# Filament-Dehumidifier
A simple and cheap 3D printer filament dehumidifier.

I recently got into 3D printing and started to notice the quality of my prints deteriorating with time. After a bit of research I found that this was due to my filament picking up ambient humidity. After pricing some de-humidifiers I found them to be completely overpriced for what they did and so I decided to make my own. The hardware that you will need is listed below:

1.) Sonoff basic --> https://www.aliexpress.com/item/32760053192.html. Cost is around $6.50.
2.) 12V Computer fans x 2 --> https://www.aliexpress.com/item/32962539330.html OR use your own. Cost is around $1.90 a piece.
3.) 12V PSU to drive fans x 1 --> https://www.aliexpress.com/item/1000008001664.html. Cost is around $1.90 a piece.
4.) 3 x light fittings (screw type is recommended). Get them from a hardware store. Cost should be around $0.7 a piece.
5.) 3 x Incandescent, 60W light bulbs. Cost should also be around $0.7 a piece.
6.) 1 x DHT22 temperature and humidity sensor. Get the one with only three pins on the red PCB. --> https://www.aliexpress.com/item/32675222015.html. Cost is around $2.50.
7.) Solid thermal container (not the bag type). You could use a cooler box (South African English), Esky (Australian English), Chilly bin (NZ)... Most people have one of these laying around the house.
8.) Frame to hold the assembly within the thermal container.
9.) Bag of silica based kitty litter. You may also want to throw in a small bag of colour changing silica to know when it needs drying out.

You may also need a USB to serial converter to program the sonoff. --> https://www.aliexpress.com/item/32481520135.html

Sonoff instructions:
1.) Follow the programming instructions here but take note of the additional points below. --> https://medium.com/@jeffreyroshan/flashing-a-custom-firmware-to-sonoff-wifi-switch-with-arduino-ide-402e5a2f77b
2.) In the above link you may not need to use header pins. You could just solder directly onto the PCB since all updates after the first programming process will be OTA.
3.) Take special note in the programming instructions above that you should not connect the 3.3V from the programmer and plug the Sonoff into mains at the same time.
4.) Do not use the latest version of the ESP8266 library for Arduino. Use version 2.4.2 as this was the last version where WiFi still worked reliably.
5.) Add the DHT adafruit library. Do not use the ESP8266 DHT library as this will break the webserver and adds no benefit.
5.) You may need to change the flash type to DIO before programming.
6.) Change the SSID and password in the sketch to match yours. The variables are in the start of the code and are easy to identify.
7.) Compile and program.
8.) Make sure that the Sonoff is connecting to your WiFi (The LED should light up and the turn off within a few seconds. If it stays on then it's not connecting) and then remove the programming cables.
9.) Connect the DHT sensor to the Sonoff by connecting the 3.3V and GND to the respective Sonoff pins. The data pin should connect to GPIO 14 which is the final pin on the Sonoff header (furthest from the button).
10.) Mount the DHT to the outside of the Sonoff. You will need to drill a few small holes in the plastic roof of the sonoff to get the wires out.

Assembly instructions:

NB!!! You will be working with mains voltage. Be very careful and never connect to mains before you have triple checked the connections and insulated them.

1.) Build a frame out of a material of choice (wood would be easy) which holds the bulbs above where the filament will lay within the thermal container. The frame should fit within the thermal container with the legs ideally being at the corners to maximise the space underneath it.
2.) Mount the three bulbs and fittings at the top of the frame.
3.) Mount the fans at the top of the frame so that they are blowing down. Connect them in parallel and run the 12V lead down near to the sonoff.
4.) Mount the sonoff assembly at the base of one of the legs. Try to keep the DHT out of the direct line of sight of the bulbs so that it detects the air temperature.
5.) Bring in the mains feed (still unpowered) from outside the cooler and connect it to the 12V power supply and the input of the sonoff. You may need to use a choc block or Wago connectors to split the feed. Make sure it is well insulated when done. Using Wago connectors will do this for you. Unless there is some sort of the drainage plug on the cooler that you can use to run the wire through you may need to keep the lid slightly open to feed the wire in.
6.) Connect the output of the sonoff to the bulb feed.
7.) Pour the silica into the bottom of the cooler and spread it around.
8.) Once you send power into the cooler the system should be operational. Place the filament on top of the silica and leave it for 4-5 hours for petg and 10 hours for pla.

Use:

1.) Check the status of the system by accessing dehumidifier.local:8080/state
2.) Set the type of filament by accessing dehumidifier.local:8080/filament?type=<type> where <type> can be 0 for pla or 1 for petg.
3.) Upgrade the firmware by accessing
