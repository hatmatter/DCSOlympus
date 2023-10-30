#include "groundunit.h"
#include "utils.h"
#include "logger.h"
#include "commands.h"
#include "scheduler.h"
#include "defines.h"
#include "unitsmanager.h"

#include <GeographicLib/Geodesic.hpp>
using namespace GeographicLib;

extern Scheduler* scheduler;
extern UnitsManager* unitsManager;
json::value GroundUnit::database = json::value();

#define RANDOM_ZERO_TO_ONE (double)(rand()) / (double)(RAND_MAX)
#define RANDOM_MINUS_ONE_TO_ONE (((double)(rand()) / (double)(RAND_MAX) - 0.5) * 2)

void GroundUnit::loadDatabase(string path) {
	char* buf = nullptr;
	size_t sz = 0;
	if (_dupenv_s(&buf, &sz, "DCSOLYMPUS_PATH") == 0 && buf != nullptr)
	{
		std::ifstream ifstream(string(buf) + path);
		std::stringstream ss;
		ss << ifstream.rdbuf();
		std::error_code errorCode;
		database = json::value::parse(ss.str(), errorCode);
		if (database.is_object())
			log("Ground Units database loaded correctly");
		else
			log("Error reading Ground Units database file");

		free(buf);
	}
}

/* Ground unit */
GroundUnit::GroundUnit(json::value json, unsigned int ID) : Unit(json, ID)
{
	log("New Ground Unit created with ID: " + to_string(ID));

	setCategory("GroundUnit");
	setDesiredSpeed(10);
};

void GroundUnit::setDefaults(bool force)
{
	if (!getAlive() || !getControlled() || getHuman() || !getIsLeader()) return;

	/* Set the default IDLE state */
	setState(State::IDLE);

	/* Set the default options */
	setROE(ROE::OPEN_FIRE_WEAPON_FREE, force);
	setOnOff(onOff, force);
	setFollowRoads(followRoads, force);
}

void GroundUnit::setState(unsigned char newState)
{
	/************ Perform any action required when LEAVING a state ************/
	if (newState != state) {
		switch (state) {
		case State::IDLE: {
			break;
		}
		case State::REACH_DESTINATION: {
			break;
		}
		case State::FIRE_AT_AREA: {
			setTargetPosition(Coords(NULL));
			break;
		}
		case State::SIMULATE_FIRE_FIGHT: {
			setTargetPosition(Coords(NULL));
			break;
		}
		case State::SCENIC_AAA: {
			setTargetPosition(Coords(NULL));
			break;
		}
		case State::MISS_ON_PURPOSE: {
			setTargetPosition(Coords(NULL));
			break;
		}
		default:
			break;
		}
	}

	/************ Perform any action required when ENTERING a state ************/
	switch (newState) {
	case State::IDLE: {
		setEnableTaskCheckFailed(false);
		clearActivePath();
		resetActiveDestination();
		break;
	}
	case State::REACH_DESTINATION: {
		setEnableTaskCheckFailed(true);
		resetActiveDestination();
		break;
	}
	case State::FIRE_AT_AREA: {
		setEnableTaskCheckFailed(true);
		clearActivePath();
		resetActiveDestination();
		break;
	}
	case State::SIMULATE_FIRE_FIGHT: {
		setEnableTaskCheckFailed(true);
		clearActivePath();
		resetActiveDestination();
		break;
	}
	case State::SCENIC_AAA: {
		setEnableTaskCheckFailed(false);
		clearActivePath();
		resetActiveDestination();
		break;
	}
	case State::MISS_ON_PURPOSE: {
		setEnableTaskCheckFailed(false);
		clearActivePath();
		resetActiveDestination();
		break;
	}
	default:
		break;
	}

	resetTask();

	log(unitName + " setting state from " + to_string(state) + " to " + to_string(newState));
	state = newState;

	triggerUpdate(DataIndex::state);
}

void GroundUnit::AIloop()
{
	switch (state) {
	case State::IDLE: {
		setTask("Idle");
		if (getHasTask())
			resetTask();
		break;
	}
	case State::REACH_DESTINATION: {
		string enrouteTask = "";
		bool looping = false;

		std::ostringstream taskSS;
		taskSS << "{ id = 'FollowRoads', value = " << (getFollowRoads() ? "true" : "false") << " }";
		enrouteTask = taskSS.str();

		if (activeDestination == NULL || !getHasTask())
		{
			if (!setActiveDestination())
				setState(State::IDLE);
			else
				goToDestination(enrouteTask);
		}
		else {
			if (isDestinationReached(GROUND_DEST_DIST_THR)) {
				if (updateActivePath(looping) && setActiveDestination())
					goToDestination(enrouteTask);
				else
					setState(State::IDLE);
			}
		}
		break;
	}
	case State::FIRE_AT_AREA: {
		setTask("Firing at area");

		if (!getHasTask()) {
			std::ostringstream taskSS;
			taskSS.precision(10);
			taskSS << "{id = 'FireAtPoint', lat = " << targetPosition.lat << ", lng = " << targetPosition.lng << ", radius = 1000}";
			Command* command = dynamic_cast<Command*>(new SetTask(groupName, taskSS.str(), [this]() { this->setHasTaskAssigned(true); }));
			scheduler->appendCommand(command);
			setHasTask(true);
		}

		break;
	}
	case State::SIMULATE_FIRE_FIGHT: {
		setTask("Simulating fire fight");

		if (!getHasTask() || internalCounter == 0) {
			double dist;
			double bearing1;
			double bearing2;
			Geodesic::WGS84().Inverse(position.lat, position.lng, targetPosition.lat, targetPosition.lng, dist, bearing1, bearing2);

			double r = 15; /* m */
			/* Default gun values */
			double barrelHeight = 1.0; /* m */
			double muzzleVelocity = 860; /* m/s */
			if (database.has_object_field(to_wstring(name))) { 
				json::value databaseEntry = database[to_wstring(name)];
				if (databaseEntry.has_number_field(L"barrelHeight") && databaseEntry.has_number_field(L"muzzleVelocity")) {
					barrelHeight = databaseEntry[L"barrelHeight"].as_number().to_double();
					muzzleVelocity = databaseEntry[L"muzzleVelocity"].as_number().to_double();
				}
			}

			double barrelElevation = r * (9.81 * dist / (2 * muzzleVelocity * muzzleVelocity) + (targetPosition.alt - (position.alt + barrelHeight)) / dist);	/* m */
			
			double lat = 0;
			double lng = 0;
			double randomBearing = bearing1 + (((double)(rand()) / (double)(RAND_MAX) - 0.5) * 2) * 0; // TODO put defined constant here
			Geodesic::WGS84().Direct(position.lat, position.lng, randomBearing, r, lat, lng);

			std::ostringstream taskSS;
			taskSS.precision(10);
			taskSS << "{id = 'FireAtPoint', lat = " << lat << ", lng = " << lng << ", alt = " << position.alt + barrelElevation + barrelHeight << ", radius = 0.001}";
			Command* command = dynamic_cast<Command*>(new SetTask(groupName, taskSS.str(), [this]() { this->setHasTaskAssigned(true); }));
			scheduler->appendCommand(command);
			setHasTask(true);
		}

		if (internalCounter == 0)
			internalCounter = 20 / FRAMERATE_TIME_INTERVAL;
		internalCounter--;

		break;
	}
	case State::SCENIC_AAA: {
		setTask("Scenic AAA");

		if ((!getHasTask() || internalCounter == 0) && getOperateAs() > 0) {
			double distance = 0;
			unsigned char targetCoalition = getOperateAs() == 2 ? 1 : 2;
			Unit* target = unitsManager->getClosestUnit(this, targetCoalition, { "Aircraft", "Helicopter" }, distance);

			/* Only run if an enemy air unit is closer than 20km to avoid useless load */
			if (distance < 20000 /* m */) {
				double r = 15; /* m */
				double barrelElevation = r * tan(acos(((double)(rand()) / (double)(RAND_MAX))));

				double lat = 0;
				double lng = 0;
				double randomBearing = ((double)(rand()) / (double)(RAND_MAX)) * 360;
				Geodesic::WGS84().Direct(position.lat, position.lng, randomBearing, r, lat, lng);

				std::ostringstream taskSS;
				taskSS.precision(10);
				taskSS << "{id = 'FireAtPoint', lat = " << lat << ", lng = " << lng << ", alt = " << position.alt + barrelElevation << ", radius = 0.001}";
				Command* command = dynamic_cast<Command*>(new SetTask(groupName, taskSS.str(), [this]() { this->setHasTaskAssigned(true); }));
				scheduler->appendCommand(command);
				setHasTask(true);
			}
		}

		if (internalCounter == 0)
			internalCounter = 20 / FRAMERATE_TIME_INTERVAL;
		internalCounter--;

		break;
	}
	case State::MISS_ON_PURPOSE: {
		setTask("Missing on purpose");

		/* Only run this when the internal counter reaches 0 to avoid excessive computations when no nearby target */
		if (internalCounter == 0 && getOperateAs() > 0) {
			double distance = 0;
			unsigned char targetCoalition = getOperateAs() == 2 ? 1 : 2;
			Unit* target = unitsManager->getClosestUnit(this, targetCoalition, {"Aircraft", "Helicopter"}, distance);

			/* Default gun values */
			double barrelHeight = 1.0; /* m */
			double muzzleVelocity = 860; /* m/s */
			double aimTime = 10; /* s */
			unsigned int shotsToFire = 10;
			double shotsBaseInterval = 15; /* s */
			double shotsBaseScatter = 5; /* degs */
			double engagementRange = 10000; /* m */
			
			if (database.has_object_field(to_wstring(name))) {
				json::value databaseEntry = database[to_wstring(name)];
				if (databaseEntry.has_number_field(L"barrelHeight"))
					barrelHeight = databaseEntry[L"barrelHeight"].as_number().to_double();
				if (databaseEntry.has_number_field(L"muzzleVelocity"))
					muzzleVelocity = databaseEntry[L"muzzleVelocity"].as_number().to_double();
				if (databaseEntry.has_number_field(L"aimTime"))
					aimTime = databaseEntry[L"aimTime"].as_number().to_double();
				if (databaseEntry.has_number_field(L"shotsToFire"))
					shotsToFire = databaseEntry[L"shotsToFire"].as_number().to_uint32();
				if (databaseEntry.has_number_field(L"engagementRange"))
					engagementRange = databaseEntry[L"engagementRange"].as_number().to_double();
				if (databaseEntry.has_number_field(L"shotsBaseInterval"))
					shotsBaseInterval = databaseEntry[L"shotsBaseInterval"].as_number().to_double();
				if (databaseEntry.has_number_field(L"shotsBaseScatter"))
					shotsBaseScatter = databaseEntry[L"shotsBaseScatter"].as_number().to_double();
			}

			/* Only do if we have a valid target close enough for AAA */
			if (target != nullptr && distance < 10000 /* m */) {
				/* Approximate the flight time */
				if (muzzleVelocity != 0)
					aimTime += distance / muzzleVelocity; 

				internalCounter = (aimTime + (3 - shotsIntensity) * shotsBaseInterval + 2) / FRAMERATE_TIME_INTERVAL;

				/* Compute where the target will be in aimTime seconds. */
				double aimDistance = target->getHorizontalVelocity() * aimTime;
				double aimLat = 0;
				double aimLng = 0;
				Geodesic::WGS84().Direct(target->getPosition().lat, target->getPosition().lng, target->getHeading() * 57.29577, aimDistance, aimLat, aimLng); /* TODO make util function */
				double aimAlt = target->getPosition().alt + target->getVerticalVelocity() * aimTime;

				/* Compute a random scattering depending on the distance and the selected shots scatter */
				double scatterDistance = distance * tan(shotsBaseScatter * (3 - shotsScatter) / 57.29577) * RANDOM_MINUS_ONE_TO_ONE;
				double scatterAngle = 180 * RANDOM_MINUS_ONE_TO_ONE;
				Geodesic::WGS84().Direct(aimLat, aimLng, scatterAngle, scatterDistance, aimLat, aimLng); /* TODO make util function */
				aimAlt = aimAlt + distance * tan(shotsBaseScatter * (3 - shotsScatter) / 57.29577) * RANDOM_MINUS_ONE_TO_ONE;

				/* Send the command */
				std::ostringstream taskSS;
				taskSS.precision(10);
				taskSS << "{id = 'FireAtPoint', lat = " << aimLat << ", lng = " << aimLng << ", alt = " << aimAlt << ", radius = 0.001, expendQty = " << shotsToFire << " }";
				Command* command = dynamic_cast<Command*>(new SetTask(groupName, taskSS.str(), [this]() { this->setHasTaskAssigned(true); }));
				scheduler->appendCommand(command);
				setHasTask(true);

				setTargetPosition(Coords(aimLat, aimLng, target->getPosition().alt));
			}	
			else {
				if (getHasTask())
					resetTask();
			}
		}

		/* If no valid target was detected */
		if (internalCounter == 0) {
			double alertnessTimeConstant = 10; /* s */
			if (database.has_object_field(to_wstring(name))) {
				json::value databaseEntry = database[to_wstring(name)];
				if (databaseEntry.has_number_field(L"alertnessTimeConstant"))
					alertnessTimeConstant = databaseEntry[L"alertnessTimeConstant"].as_number().to_double();
			}
			internalCounter = (5 + RANDOM_ZERO_TO_ONE * alertnessTimeConstant) / FRAMERATE_TIME_INTERVAL;
		}
		internalCounter--;

		break;
	}
	default:
		break;
	}
}

void GroundUnit::changeSpeed(string change)
{
	if (change.compare("stop") == 0)
		setState(State::IDLE);
	else if (change.compare("slow") == 0)
		setDesiredSpeed(getDesiredSpeed() - knotsToMs(5));
	else if (change.compare("fast") == 0)
		setDesiredSpeed(getDesiredSpeed() + knotsToMs(5));

	if (getDesiredSpeed() < 0)
		setDesiredSpeed(0);
}

void GroundUnit::setOnOff(bool newOnOff, bool force) 
{
	if (newOnOff != onOff || force) {
		Unit::setOnOff(newOnOff, force);
		Command* command = dynamic_cast<Command*>(new SetOnOff(groupName, onOff));
		scheduler->appendCommand(command);
	}
}

void GroundUnit::setFollowRoads(bool newFollowRoads, bool force)
{
	if (newFollowRoads != followRoads || force) {
		Unit::setFollowRoads(newFollowRoads, force);
		resetActiveDestination(); /* Reset active destination to apply option*/
	}
}
