"""Editable diagrams: one geometry model produces SVG and diagrams.net files.
All connector waypoints and label positions are explicit; no automatic routing.
"""
from pathlib import Path
from html import escape
import json
W,H=1200,720
OUT=Path(__file__).resolve().parents[1]/'diagrams'
PAL={'bus':('#e8f1fb','#35638a'),'student':('#e9f4eb','#477a51'),'neutral':('#f4f6f8','#667485'),'safe':('#fff0ed','#a74c40')}
class Diagram:
 def __init__(self,title,subtitle,height=720):
  self.nodes=[];self.edges=[];self.height=height
  self.text('title',30,12,1140,40,title,28,True)
  self.text('subtitle',30,57,1140,30,subtitle,18)
 def text(self,id,x,y,w,h,label,size=19,bold=False):
  self.nodes.append(dict(id=id,x=x,y=y,w=w,h=h,label=label,size=size,bold=bold,kind='text'))
 def box(self,id,x,y,w,h,label,color='bus',size=21):
  self.nodes.append(dict(id=id,x=x,y=y,w=w,h=h,label=label,size=size,bold=False,kind='box',color=color))
 def dot(self,id,x,y):self.nodes.append(dict(id=id,x=x-9,y=y-9,w=18,h=18,label='',size=1,bold=False,kind='dot'))
 def edge(self,id,pts,label='',pos=None,color='#35638a',both=False,dashed=False,src=None,tgt=None):
  self.edges.append(dict(id=id,pts=pts,color=color,both=both,dashed=dashed,src=src,tgt=tgt))
  if label:
   x,y,w,h=pos;self.text(id+'-label',x,y,w,h,label,18)
 def save(self,name):
  s=[f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{self.height}" viewBox="0 0 {W} {self.height}"><rect width="1200" height="{self.height}" fill="white"/>','<defs><marker id="arrow" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto-start-reverse"><path d="M0 0 L8 4 L0 8 z" fill="context-stroke"/></marker></defs>']
  cells=['<mxCell id="0"/>','<mxCell id="1" parent="0"/>']
  for n in self.nodes:
   x,y,w,h=n['x'],n['y'],n['w'],n['h'];kind=n['kind'];fill,stroke=PAL.get(n.get('color'),('white','none'))
   if kind=='dot':s.append(f'<circle cx="{x+w/2}" cy="{y+h/2}" r="9" fill="#202a33"/>')
   elif kind=='box':s.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="14" fill="{fill}" stroke="{stroke}" stroke-width="2"/>')
   style=f'rounded=1;whiteSpace=wrap;html=0;fillColor={fill};strokeColor={stroke};fontColor=#202a33;fontFamily=Arial;fontSize={n["size"]};fontStyle={int(n["bold"])};spacing=10;'
   if kind=='text':style+='fillColor=none;strokeColor=none;'
   if kind=='dot':style='ellipse;fillColor=#202a33;strokeColor=none;'
   cells.append(f'<mxCell id="{n["id"]}" value="{escape(n["label"],quote=True).replace(chr(10), "&#xa;")}" style="{style}" vertex="1" parent="1"><mxGeometry x="{x}" y="{y}" width="{w}" height="{h}" as="geometry"/></mxCell>')
  for e in self.edges:
   pts=e['pts'];path='M '+' L '.join(f'{x},{y}' for x,y in pts)
   s.append(f'<path d="{path}" fill="none" stroke="{e["color"]}" stroke-width="2.3" marker-end="url(#arrow)"'+(' marker-start="url(#arrow)"' if e['both'] else '')+(' stroke-dasharray="7 5"' if e['dashed'] else '')+'/>')
   style=f'edgeStyle=none;rounded=0;strokeColor={e["color"]};strokeWidth=2;endArrow=block;endFill=1;'+('startArrow=block;startFill=1;' if e['both'] else '')+('dashed=1;' if e['dashed'] else '')
   attrs=''
   for end,key,point in [('exit','src',pts[0]),('entry','tgt',pts[-1])]:
    if e[key]:
     n=next(n for n in self.nodes if n['id']==e[key]);attrs+=f' {"source" if key=="src" else "target"}="{e[key]}"';style+=f'{end}X={(point[0]-n["x"])/n["w"]};{end}Y={(point[1]-n["y"])/n["h"]};{end}Perimeter=0;'
   geometry=f'<mxPoint x="{pts[0][0]}" y="{pts[0][1]}" as="sourcePoint"/><mxPoint x="{pts[-1][0]}" y="{pts[-1][1]}" as="targetPoint"/>'
   if len(pts)>2:geometry+='<Array as="points">'+''.join(f'<mxPoint x="{x}" y="{y}"/>' for x,y in pts[1:-1])+'</Array>'
   cells.append(f'<mxCell id="{e["id"]}" style="{style}" edge="1" parent="1"{attrs}><mxGeometry relative="1" as="geometry">{geometry}</mxGeometry></mxCell>')
  # Text is laid out explicitly, never attached to an automatic edge midpoint.
  for n in self.nodes:
   lines=n['label'].split('\n');lh=n['size']*1.25
   for i,line in enumerate(lines):
    y=n['y']+n['h']/2-(len(lines)-1)*lh/2+n['size']*.35+i*lh
    s.append(f'<text x="{n["x"]+n["w"]/2}" y="{y}" text-anchor="middle" font-family="Arial, sans-serif" font-size="{n["size"]}" font-weight="{700 if n["bold"] else 400}" fill="#202a33">{escape(line)}</text>')
  s.append('</svg>');OUT.mkdir(exist_ok=True)
  (OUT/(name+'.svg')).write_text('\n'.join(s))
  xml=f'<mxfile host="app.diagrams.net"><diagram name="{name}" id="{name}"><mxGraphModel page="1" pageWidth="{W}" pageHeight="{self.height}"><root>'+''.join(cells)+'</root></mxGraphModel></diagram></mxfile>'
  (OUT/(name+'.drawio')).write_text(xml)
  (OUT/(name+'.layout.json')).write_text(json.dumps({'nodes':self.nodes,'edges':self.edges},indent=2))

def system():
 d=Diagram('System architecture','Teensy owns spacecraft authority; Pi serves the mission payload.')
 d.box('ground',35,150,235,115,'Yamcs ground\nRF bridge', 'neutral')
 d.box('core',480,135,280,145,'Teensy 4.1 / Zephyr\nMissionApp + bus services\nHardware watchdog')
 d.box('pi',930,135,235,145,'Pi Zero W / Linux\nPayload software\nSoftware heartbeat','student')
 d.box('sensors',35,450,235,110,'Bus sensors\nGPS / IMU / thermal')
 d.box('pdu',480,450,280,110,'PDU\nPower + actuation\nHardware watchdogs')
 d.box('piswitch',810,330,195,90,'OBC U2\nPi load switch','neutral',20)
 d.box('payload',930,450,235,110,'One logical payload\nMission hardware','student')
 d.edge('rf',[(270,195),(480,195)],'RF command / TM',(275,151,200,35),both=True,src='ground',tgt='core')
 d.edge('ipc',[(760,195),(930,195)],'UART link',(770,154,150,30),both=True,src='core',tgt='pi')
 d.edge('sense',[(270,500),(345,500),(345,250),(480,250)],'Sensor I/O',(355,345,130,40),both=True,src='sensors',tgt='core')
 d.edge('pductl',[(620,280),(620,450)],'PDU commands\nand status',(635,325,180,65),both=True,src='core',tgt='pdu')
 d.edge('data',[(1050,280),(1050,450)],'Payload\ndata / control',(1063,325,130,65),both=True,src='pi',tgt='payload')
 d.edge('paypower',[(760,500),(930,500)],'Switched power',(766,515,160,40),color='#a84737',src='pdu',tgt='payload')
 d.edge('pibus',[(760,470),(780,470),(780,390),(810,390)],'BUS_5V',(805,432,125,35),color='#a84737',src='pdu',tgt='piswitch')
 d.edge('pienable',[(760,250),(790,250),(790,345),(810,345)],'Pin 36\nRPI_ENABLE',(795,270,125,55),src='core',tgt='piswitch')
 d.edge('pipower',[(907,330),(907,260),(930,260)],'Pi 5 V',(930,280,90,35),color='#a84737',src='piswitch',tgt='pi')
 d.text('note',65,610,1070,75,'Blue: logical data/control. Red: power. PDU BUS_5V feeds the OBC Pi load switch.\nExact as-built rails, harnesses and reset paths require hardware verification.',19)
 d.save('01_system')

def core():
 d=Diagram('Core topology','Functional groups organize reusable services around one mission application.')
 d.box('mission',420,100,360,105,'Mission\nMissionApp\nBus modes + student activities','student')
 d.box('power',35,310,340,155,'PowerThermal\nEpsManager: PDU + Pi switch\nThermalManager\nDeploymentManager')
 d.box('att',430,310,340,155,'AttitudeNavigation\nAdcsManager / GpsManager\nSensor managers\nTimeCoordinator')
 d.box('radio',825,310,340,155,'GroundLink\nCommsManager / ComQueue\nRadioManager / CCSDS\nSPI / GPIO drivers')
 d.box('services',35,570,735,115,'CoreServices: CdhCore / Health / FaultManager\nRate groups / parameters / time service / watchdog driver\nShared UART, SPI, I2C and GPIO drivers: one owner per interface','neutral',20)
 d.box('companion',825,570,340,115,'CompanionLink\nPiSupervisor + peer transport\nPi jobs / results / heartbeat',size=20)
 for id,x in [('power',205),('att',600),('radio',995)]:
  d.edge('mission-'+id,[(600,205),(600,260),(x,260),(x,310)],src='mission',tgt=id)
 d.edge('jobs',[(780,155),(1190,155),(1190,625),(1165,625)],src='mission',tgt='companion')
 d.text('shared',45,495,1100,55,'Shared-service and protective connections are omitted here for clarity (Sections 3 and 10).\nFaultManager requests Safe and bounded protection; PiSupervisor requests EPS recovery.',19)
 d.save('02_core_topology')

def payload():
 d=Diagram('Payload topology','The Pi executes Teensy-authorized jobs through Application, Manager and Driver components.')
 d.box('core',30,110,230,95,'Teensy\nMissionApp\nPiSupervisor')
 d.box('link',390,110,300,95,'CompanionLink\nJobs / results / heartbeat')
 d.box('app',870,110,300,95,'PayloadApp\nAuthorized activity','student')
 d.box('mgr',870,310,300,105,'PayloadManager\nDevice protocol + lifecycle','student')
 d.box('worker',440,310,300,105,'ProcessingWorker\nLong-running computation','student')
 d.box('store',30,310,300,125,'ProductStore / Transfer\nProducts + metadata\nRetain until ground receipt')
 d.box('sim',440,515,300,90,'SimDriver\nNo physical I/O','neutral')
 d.box('driver',870,500,300,80,'Linux driver\nUSB / UART / SPI / I2C','neutral')
 d.box('hw',870,645,300,55,'Payload hardware','student')
 d.edge('peer',[(260,157),(390,157)],both=True,src='core',tgt='link')
 d.edge('job',[(690,157),(870,157)],both=True,src='link',tgt='app')
 d.edge('request',[(1020,205),(1020,310)],'Request / result',(1030,245,160,30),both=True,src='app',tgt='mgr')
 d.edge('workerjob',[(870,365),(740,365)],both=True,src='mgr',tgt='worker')
 d.text('workerlabel',733,315,137,40,'Start / done',18)
 d.edge('real',[(1020,415),(1020,500)],both=True,src='mgr',tgt='driver')
 d.edge('simjob',[(950,415),(950,485),(590,485),(590,515)],dashed=True,both=True,src='mgr',tgt='sim')
 d.edge('hardware',[(1020,580),(1020,645)],both=True,src='driver',tgt='hw')
 d.edge('product',[(920,415),(920,460),(180,460),(180,435)],src='mgr',tgt='store')
 d.text('productlabel',390,422,260,32,'Completed product',18)
 d.edge('transfer',[(180,310),(180,250),(540,250),(540,205)],both=True,src='store',tgt='link')
 d.text('choice',410,625,350,65,'Select real OR simulated driver.\nWorker communicates only with manager.',18)
 d.save('03_payload_topology')

def modes():
 d=Diagram('Spacecraft mode state machine','Bus-owned mode logic hosted by MissionApp; hardware protection has a separate deadline.')
 d.dot('initial',65,185)
 d.box('startup',130,135,280,100,'STARTUP\nEstablish safe readiness')
 d.box('detumble',800,135,300,100,'DETUMBLE\nBounded rate reduction')
 d.box('nominal',800,480,300,100,'NOMINAL\nAdmit mission activities')
 d.box('safe',130,480,280,100,'SAFE\nProtect bus\nRetain commanding','safe')
 d.edge('init',[(74,185),(130,185)],color='#202a33',src='initial',tgt='startup')
 d.edge('ready',[(410,185),(800,185)],'ready [no latch; inhibits satisfied]',(435,137,340,40),color='#202a33',src='startup',tgt='detumble')
 d.edge('settled',[(950,235),(950,480)],'rates low\n[dwell complete]',(975,315,210,65),color='#202a33',src='detumble',tgt='nominal')
 d.edge('fault',[(800,530),(410,530)],'critical fault or ground Safe',(435,482,350,40),color='#a84737',src='nominal',tgt='safe')
 d.edge('startfault',[(190,235),(190,480)],'fault / ground Safe\nor boot lockout',(15,330,165,65),color='#a84737',src='startup',tgt='safe')
 d.edge('recover',[(345,480),(345,235)],'recover [clear dwell;\nlatch cleared by ground\nwhen required]',(365,275,270,95),color='#202a33',src='safe',tgt='startup')
 d.edge('detfault',[(850,235),(410,500)],'fault / ground Safe / timeout\nor invalid sensing: request coast',(595,355,320,65),color='#a84737',src='detumble',tgt='safe')
 d.text('latch',50,620,1100,65,'Startup lockout: persistent latch, reset limit or invalid reset record selects Safe.\nGround clearance never bypasses inhibits or current recovery guards.',19)
 d.save('04_modes')

def layers():
 d=Diagram('Application - Manager - Driver','Three layers. Protocol knowledge belongs in the manager; the driver owns the hardware interface.')
 for x,t in [(35,'APPLICATION'),(440,'MANAGER'),(845,'DRIVER')]:d.text('h'+t,x,115,320,40,t,23,True)
 d.box('app',35,215,320,125,'MissionApp\nRequest payload or Pi power','student')
 d.box('mgr',440,215,320,125,'EpsManager\nPower policy + PDU protocol')
 d.box('drv',845,215,320,125,'UartDriver: PDU bytes\nGPIO driver: Pi switch','neutral')
 d.box('papp',35,470,320,125,'PayloadApp\nRequest acquisition','student')
 d.box('pmgr',440,470,320,125,'PayloadManager\nDevice protocol + lifecycle','student')
 d.box('pdrv',845,470,320,125,'Linux driver\nPayload interface I/O','neutral')
 for a,b,x1,x2,y in [('app','mgr',355,440,277),('mgr','drv',760,845,277),('papp','pmgr',355,440,532),('pmgr','pdrv',760,845,532)]:d.edge(a+b,[(x1,y),(x2,y)],both=True,src=a,tgt=b)
 d.text('return',90,370,1020,55,'Requests use manager/driver ports; callbacks return results without upward code dependencies.',19)
 d.text('sim',80,635,1040,55,'Simulation replaces the hardware-interface driver. Managers and application contracts remain.\nA high-level stub is permitted for demos, with reduced protocol-test coverage stated explicitly.',19)
 d.save('05_layers')

def sequence():
 d=Diagram('Reference mission sequence','Yamcs uses the RF/ground receiver to reach the Teensy bus authority.',900)
 xs=[110,350,585,825,1090];ids=['ground','rf','core','pdu','pi'];labels=['Yamcs / operator','RF / ground receiver','Teensy\nMissionApp + bus','PDU','Pi\nPayload + storage']
 for id,x,label in zip(ids,xs,labels):
  d.box(id,x-100,105,200,65,label,'neutral',19)
  d.edges.append(dict(id=id+'-life',pts=[(x,170),(x,842)],color='#9ba7b4',both=False,dashed=True,src=None,tgt=None))
 steps=[(110,350,'Schedule / command'),(350,585,'Command frame'),(585,825,'Enable payload power'),(825,585,'Applied-state reply'),(585,1090,'Authorize acquisition (job ID)'),(1090,585,'Accepted + progress'),(585,350,'Job status telemetry'),(350,110,'Job status event'),(1090,1090,'Acquire sample'),(1090,585,'Product ready + metadata'),(110,350,'Request downlink'),(350,585,'Downlink request'),(585,1090,'Grant bounded transfer'),(1090,585,'Bounded product chunks'),(585,350,'Chunks + bus telemetry'),(350,110,'Verified product'),(110,350,'Integrity receipt'),(350,585,'Receipt frame'),(585,1090,'Receipt: eligible for reclamation')]
 for i,(a,b,label) in enumerate(steps):
  y=202+i*33+18*sum(a0==b0 for a0,b0,_ in steps[:i])
  if a==b:
   d.edge('s'+str(i),[(a,y),(a+55,y),(a+55,y+12),(a,y+12)],label,(a-290,y-22,265,25),color='#35638a')
  else:d.edge('s'+str(i),[(a,y),(b,y)],label,(min(a,b),y-28,abs(b-a),25),color='#a66520' if i in [0,1,2,4,10,11,12,16,17,18] else '#35638a')
 d.text('failure',35,850,1130,45,'Orange: requests / authorization / receipts. Blue: status / data.\nTeensy checks mode, time and limits before authorization. Timeout: event + cancel + reconcile.',17)
 d.save('06_reference_sequence')
 p=OUT/'06_reference_sequence.svg';s=p.read_text()
 for x in xs:s=s.replace(f'd="M {x},170 L {x},842" fill="none" stroke="#9ba7b4" stroke-width="2.3" marker-end="url(#arrow)"',f'd="M {x},170 L {x},842" fill="none" stroke="#9ba7b4" stroke-width="2.3"')
 p.write_text(s)
 p=OUT/'06_reference_sequence.drawio';s=p.read_text();import re
 s=re.sub(r'(<mxCell id="[^\"]+-life" style=")([^\"]+)',lambda m:m[1]+m[2].replace('endArrow=block;endFill=1;','endArrow=none;'),s);p.write_text(s)

if __name__=='__main__':
 for f in [system,core,payload,modes,layers,sequence]:f()
 print('Six editable diagrams generated.')
