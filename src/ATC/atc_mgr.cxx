/******************************************************************************
 * atc_mgr.cxx
 * Written by Durk Talsma, started August 1, 2010.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 *
 **************************************************************************/


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <iostream>

#include <simgear/math/SGMath.hxx>
#include <Airports/dynamics.hxx>
#include <Airports/simple.hxx>
#include <Scenery/scenery.hxx>
#include "atc_mgr.hxx"


FGATCManager::FGATCManager() {
    networkVisible = false;
}

FGATCManager::~FGATCManager() {

}

void FGATCManager::init() {
    SGSubsystem::init();
    currentATCDialog = new FGATCDialogNew;
    currentATCDialog->init();

    int leg = 0;

    // find a reasonable controller for our user's aircraft..
    // Let's start by working out the following three scenarios: 
    // Starting on ground at a parking position
    // Starting on ground at the runway.
    // Starting in the Air
    bool onGround  = fgGetBool("/sim/presets/onground");
    string runway  = fgGetString("/sim/atc/runway");
    string airport = fgGetString("/sim/presets/airport-id");
    string parking = fgGetString("/sim/presets/parkpos");
    

    // Create an (invisible) AIAircraft represenation of the current
    // Users, aircraft, that mimicks the user aircraft's behavior.
    string callsign= fgGetString("/sim/multiplay/callsign");
    double longitude = fgGetDouble("/position/longitude-deg");
    double latitude  = fgGetDouble("/position/latitude-deg");
    double altitude  = fgGetDouble("/position/altitude-ft");
    double heading   = fgGetDouble("/orientation/heading-deg");
    double speed     = fgGetDouble("/velocities/groundspeed-kt");
    double aircraftRadius = 40; // note that this is currently hardcoded to a one-size-fits all JumboJet value. Should change later;


    ai_ac.setCallSign ( callsign  );
    ai_ac.setLongitude( longitude );
    ai_ac.setLatitude ( latitude  );
    ai_ac.setAltitude ( altitude  );
    ai_ac.setPerformance("jet_transport");

    // NEXT UP: Create a traffic Schedule and fill that with appropriate information. This we can use to flight planning.
    // Note that these are currently only defaults. 
    FGAISchedule *trafficRef = new FGAISchedule;
    trafficRef->setFlightType("gate");

    FGScheduledFlight *flight =  new FGScheduledFlight;
    flight->setDepartureAirport(airport);
    flight->setArrivalAirport(airport);
    flight->initializeAirports();
    flight->setFlightRules("IFR");
    flight->setCallSign(callsign);
    
    trafficRef->assign(flight);
    FGAIFlightPlan *fp = new FGAIFlightPlan;
    ai_ac.setTrafficRef(trafficRef);
    
    string flightPlanName = airport + "-" + airport + ".xml";
    double cruiseAlt = 100; // Doesn't really matter right now.
    double courseToDest = 180; // Just use something neutral; this value might affect the runway that is used though...
    time_t deptime = 0;        // just make sure how flightplan processing is affected by this...


    FGAirport *apt = FGAirport::findByIdent(airport); 
    FGAirportDynamics* dcs = apt->getDynamics();
    int park_index = dcs->getNrOfParkings() - 1;
    cerr << "found information: " << runway << " " << airport << ": parking = " << parking << endl;
    if (onGround) {
        while (park_index >= 0 && dcs->getParkingName(park_index) != parking) park_index--;
            if (park_index < 0) {
                  SG_LOG( SG_GENERAL, SG_ALERT,
                          "Failed to find parking position " << parking <<
                           " at airport " << airport );
             }
        if (parking.empty() || (park_index < 0)) {
            controller = apt->getDynamics()->getTowerController();
            int stationFreq = apt->getDynamics()->getTowerFrequency(2);
            cerr << "Setting radio frequency to in airfrequency: " << stationFreq << endl;
            fgSetDouble("/instrumentation/comm[0]/frequencies/selected-mhz", ((double) stationFreq / 100.0));
            leg = 4;
            string fltType = "ga";
            fp->createTakeOff(&ai_ac, false, apt, 0, fltType);
        } else {
            controller = apt->getDynamics()->getStartupController();
            int stationFreq = apt->getDynamics()->getGroundFrequency(2);
            cerr << "Setting radio frequency to : " << stationFreq << endl;
            fgSetDouble("/instrumentation/comm[0]/frequencies/selected-mhz", ((double) stationFreq / 100.0));
            leg = 2;
            //double, lat, lon, head; // Unused variables;
            //int getId = apt->getDynamics()->getParking(gateId, &lat, &lon, &head);
            FGParking* parking = dcs->getParking(park_index);
            aircraftRadius = parking->getRadius();
            string fltType = parking->getType(); // gate / ramp, ga, etc etc. 
            string aircraftType; // Unused.
            string airline;      // Currently used for gate selection, but a fallback mechanism will apply when not specified.
            fp->setGate(park_index);
            fp->createPushBack(&ai_ac,
                               false, 
                               apt, 
                               latitude,
                               longitude,
                               aircraftRadius,
                               fltType,
                               aircraftType,
                               airline);
         } 
     } else {
        controller = 0;
     }

    // Create an initial flightplan and assign it to the ai_ac. We won't use this flightplan, but it is necessary to
    // keep the ATC code happy. 
    
    
    fp->restart();
    fp->setLeg(leg);
    ai_ac.SetFlightPlan(fp);
    if (controller) {
        controller->announcePosition(ai_ac.getID(), fp, fp->getCurrentWaypoint()->routeIndex,
                                      ai_ac._getLatitude(), ai_ac._getLongitude(), heading, speed, altitude,
                                      aircraftRadius, leg, &ai_ac);

    //dialog.init();

   //osg::Node* node = apt->getDynamics()->getGroundNetwork()->getRenderNode();
   //cerr << "Adding groundnetWork to the scenegraph::init" << endl;
   //globals->get_scenery()->get_scene_graph()->addChild(node);
   }
}

void FGATCManager::addController(FGATCController *controller) {
    activeStations.push_back(controller);
}

void FGATCManager::update ( double time ) {
    //cerr << "ATC update code is running at time: " << time << endl;
    // Test code: let my virtual co-pilot handle ATC:
   
    

    FGAIFlightPlan *fp = ai_ac.GetFlightPlan();
        
    /* test code : find out how the routing develops */
    int size = fp->getNrOfWayPoints();
    //cerr << "Setting pos" << pos << " ";
    //cerr << "setting intentions " ;
    for (int i = 0; i < size; i++) {
        int val = fp->getRouteIndex(i);
        //cerr << val << " ";
        //if ((val) && (val != pos)) {
            //intentions.push_back(val);
            //cerr << "[done ] " << endl;
        //}
    }
    //cerr << "[done ] " << endl;
    double longitude = fgGetDouble("/position/longitude-deg");
    double latitude  = fgGetDouble("/position/latitude-deg");
    double heading   = fgGetDouble("/orientation/heading-deg");
    double speed     = fgGetDouble("/velocities/groundspeed-kt");
    double altitude  = fgGetDouble("/position/altitude-ft");
    ai_ac.setLatitude(latitude);
    ai_ac.setLongitude(longitude);
    ai_ac.setAltitude(altitude);
    ai_ac.setHeading(heading);
    ai_ac.setSpeed(speed);
    ai_ac.update(time);
    controller = ai_ac.getATCController();
    currentATCDialog->update(time);
    if (controller) {
       

        //cerr << "Running FGATCManager::update()" << endl;
        controller->updateAircraftInformation(ai_ac.getID(),
                                              latitude,
                                              longitude,
                                              heading,
                                              speed,
                                              altitude, time);
        //string airport = fgGetString("/sim/presets/airport-id");
        //FGAirport *apt = FGAirport::findByIdent(airport); 
        // AT this stage we should update the flightplan, so that waypoint incrementing is conducted as well as leg loading. 
       static SGPropertyNode_ptr trans_num = globals->get_props()->getNode("/sim/atc/transmission-num", true);
            int n = trans_num->getIntValue();
        if (n == 1) {
            cerr << "Toggling ground network visibility " << networkVisible << endl;
            networkVisible = !networkVisible;
            trans_num->setIntValue(-1);
        }
        controller->render(networkVisible);

        //cerr << "Adding groundnetWork to the scenegraph::update" << endl;
   }
   //globals->get_scenery()->get_scene_graph()->addChild(node);
}