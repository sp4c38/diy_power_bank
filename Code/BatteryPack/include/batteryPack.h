#include <map>

#include "register.h"

class BatteryPack {
	public:
		BatteryPack(); // Constructor
	
		std::map<SysStatusOpt, bool> sysStatus = {
			{SysStatusOpt::CC_READY, 0},
			{SysStatusOpt::OVRD_ALERT, 0},
			{SysStatusOpt::UV, 0},
			{SysStatusOpt::OV, 0},
			{SysStatusOpt::SCD, 0},
			{SysStatusOpt::OCD, 0}
		}; 
		std::map<BalanceOpt, bool> balanceCells = {
			{BalanceOpt::CB1, false},
			{BalanceOpt::CB2, false},
			{BalanceOpt::CB5, false}
		};
		std::map<uint8_t, uint16_t> voltages = {
			{registerMap::VC1_HI, 0},
			{registerMap::VC2_HI, 0},
			{registerMap::VC5_HI, 0}
		};
		std::map<SysControlOpt, bool> sysControl1 = {
			{SysControlOpt::ADC_EN, 1},
			{SysControlOpt::TEMP_SEL, 0},
			{SysControlOpt::SHUT_A, 0},
			{SysControlOpt::SHUT_B, 0}
		}; 
		std::map<SysControlOpt, bool> sysControl2 = {
			{SysControlOpt::DELAY_DIS, 0},
			{SysControlOpt::CC_EN, 1},
			{SysControlOpt::CC_ONESHOT, 0},
			{SysControlOpt::DSG_ON, 1},
			{SysControlOpt::CHG_ON, 1}
		};
		float temp; 
		uint16_t voltage; 
		int16_t current;
		BatteryState state = BatteryState::Idle; 
		uint adcGain;
		int8_t adcOffset;
		
		void readOffsetAndGain();
		void updateTemperature();
		void updateSysStatus();
		void updateSysControl(); 
		void updateVoltages(); 
		void updateCurrent();
	
		void transitionToSHIPMode(); 
		void pushBalancing();
		void pushProtection(); 
		void pushControl(); 
};
