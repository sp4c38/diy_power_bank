#include <map>

#include "register.h"

class BatteryPack {
	public:
		// Maps will be sorted automatically based on the key.

		std::map<SysStatusOpt, bool> sysStatus = {
			{SysStatusOpt::CC_READY, false},
			{SysStatusOpt::OVRD_ALERT, false},
			{SysStatusOpt::UV, false},
			{SysStatusOpt::OV, false},
			{SysStatusOpt::SCD, false},
			{SysStatusOpt::OCD, false}
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
			{SysControlOpt::ADC_EN, true},
			{SysControlOpt::TEMP_SEL, false},
			{SysControlOpt::SHUT_A, false},
			{SysControlOpt::SHUT_B, false}
		}; 
		std::map<SysControlOpt, bool> sysControl2 = {
			{SysControlOpt::DELAY_DIS, false},
			{SysControlOpt::CC_EN, true},
			{SysControlOpt::CC_ONESHOT, false},
			{SysControlOpt::DSG_ON, true},
			{SysControlOpt::CHG_ON, true}
		};
		float temp; 
		uint16_t voltage; 
		int16_t current;
		BatteryState state = BatteryState(BatteryState::State::Idle);
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
        bool checkIfCellsAreAdjacent(const BalanceOpt a, const BalanceOpt b);
        void pushProtection();
        void pushControl();
};
