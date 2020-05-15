#include "../include/pwmClock.hpp"
#include <GLFW/glfw3.h>

float::numTicks[OUT_SOCKETS] = 
{
	         //nor         dot (3/2)        trip (2/3)
	/*1/1*/  4.f,     1.5f * 4.f,     0.66666666f * 4.f,   
	/*1/2*/	 2.f,     1.5f * 2.f,     0.66666666f * 2.f,   
	/*1/4*/	 1.f,     1.5f * 1.f,     0.66666666f * 1.f,   
	/*1/8*/	 0.5f,    1.5f * 0.5f,    0.66666666f * 0.5f,  
	/*1/16*/ 0.25f,   1.5f * 0.25f,   0.66666666f * 0.25f, 
	/*1/32*/ 0.125f,  1.5f * 0.125f,  0.66666666f * 0.125f,
	/*1/64*/ 0.0625f, 1.5f * 0.0625f, 0.66666666f * 0.0625f
};

void PwmClock::on_loaded()
{
	current_status = false;
	bpm = 0;
	swing = 0;
	sampleRate= 0;
	_reset();
}

void PwmClock::_reset()
{
	optimize_manualStep = false;
	float now = APP->engine->getSampleTime();
	sampleRate= 0;

	for(int k = 0; k < OUT_SOCKETS; k++)
	{
		sa_timer[k].Reset(now);
		odd_beat[k] = false;
	}
}

void PwmClock::updateBpm(float sr)
{
	bool updated = false;
	process_keys();
	float new_bpm = clamp(roundf(getModulableParam(this, BPM, EXT_BPM, BPM_MINVALUE, BPM_MAXVALUE)), BPM_MINVALUE, BPM_MAXVALUE);
	
	if(bpm != new_bpm)
	{
		updated = true;
		bpm = new_bpm;
	}

	if(sr != sampleRate)
	{
		sampleRate = sr;
		update = true;
	}

	duration[0] = 240.0 / bpm;	// 1/1
	duration[1] = duration[0] + duration[0] / 2.0;
	duration[2] = 2.0 * duration[0] / 3.0;

	for(int k = 1; k < 7; k++)
	{
		duration[3 * k] = duration[3 * (k - 1)] / 2.0;
		duration[3 * k + 1] = duration[3 * (k - 1) + 1] / 2.0;
		duration[3 * k + 2] = duration[3 * (k - 1) + 2] / 2.0;
	}

	float new_swing = getSwing();
	if(updated || new_swing != swing)
	{
		swing = new_swing;
		for(int k = 0; k < OUT_SOCKETS; k++)
			swingAmt[k] = duration[k] + duration[k] * swing;
	}
}

void PwmClock::process_keys()
{
	if(btnup.process(params[BPM_INC].value))
	{
		if(bpm < BPM_MAXVALUE)
		{
			bpm = round(bpm+1);
			pWidget->SetBpm(bpm);
		}
	} else if(btndwn.process(params[BPM_DEC].value))
	{
		if(bpm > BPM_MINVALUE)
		{
			bpm = round(bpm-1);
			pWidget->SetBpm(bpm);
		}
	}
}

void PwmClock::process_active(const ProcessArgs &args)
{
	onStopPulse.reset();
	onManualStep.reset();
	if(resetTrigger.process(inputs[RESET].value))
	{
		_reset();
	} else 
	{
		float pwm = getPwm();
		float now = APP->engine->getSampleTime();

		for(int k = 0; k < OUT_SOCKETS; k++)
		{
			if(outputs[OUT_1 + k].isConnected())
			{
				float gate_len = getDuration(k) * pwm;
				sa_timer[k].Step(now);
				float elps = sa_timer[k].Elapsed();
				if(elps >= getDuration(k))
				{
					elps = sa_timer[k].Reset(now);
					odd_beat[k] = !odd_beat[k];
				}
				outputs[OUT_1 + k].value = elps <= gate_len ? LVL_ON : LVL_OFF;
			}
		}
	}
}

void PwmClock::process_inactive(const ProcessArgs &args)
{
	float deltaTime = 1.0 / args.sampleRate;

	if(current_status && !onStopPulse.process(deltaTime))
		onStopPulse.trigger(PULSE_TIME);

	if(!onManualStep.process(deltaTime))
	{
		if(optimize_manualStep)
		{
			optimize_manualStep = false;
			for(int k = 0; k < OUT_SOCKETS; k++)
				outputs[OUT_1 + k].value = LVL_OFF;

			lights[ACTIVE].value = LED_OFF;
		}
		if((manualTrigger.process(params[PULSE].value) || pulseTrigger.process(inputs[PULSE_IN].value)))
		{
			onManualStep.trigger(PULSE_TIME);
			optimize_manualStep = true;
			for(int k = 0; k < OUT_SOCKETS; k++)
			{
				outputs[OUT_1 + k].value = LVL_ON;
			}
			lights[ACTIVE].value = LED_ON;
		}
	}
	outputs[ONSTOP].value = onStopPulse.process(deltaTime) ? LVL_ON : LVL_OFF;
}

bool PwmClock::isGeneratorActive()
{
	bool active = false;
	if(inputs[REMOTE_IN].isConnected()) // priorita; prioritaria
	{
		active = isSwitchOn(this, REMOTE_IN);
		if(active && !isSwitchOn(this, OFFON))
		{
			pWidget->params[OFFON]->dirtyValue = params[OFFON].value = 1.0;
		} else if(!active && isSwitchOn(this, OFFON))
		{
			pWidget->params[OFFON]->dirtyValue = params[OFFON].value = 0.0;
		}
	} else
	{
		active = isSwitchOn(this, OFFON);
	}
	return active;
}

void PwmClock::process(const ProcessArgs &args)
{
	if(pWidget == NULL)
		return;

	bool active = isGeneratorActive();

	updateBpm(args.sampleRate);

	if(active)
	{
		process_active(args);
	} else
	{
		process_inactive(args);
	}

	if(active != current_status)
	{
		current_status = active;
		_reset();
		if(active)
		{
			lights[ACTIVE].value = LED_ON;
		} else
		{
			lights[ACTIVE].value = LED_OFF;
			for(int k = 0; k < OUT_SOCKETS; k++)
				outputs[OUT_1 + k].value = LVL_OFF;
		}
	}
}

PwmClockWidget::PwmClockWidget(PwmClock *module) : SequencerWidget()
{
	if(module != NULL)
		module->setWidget(this);

	CREATE_PANEL(module, this, 15, "res/modules/PwmClock.svg");

	addParam(createParam<UPSWITCH>(Vec(mm2px(14.452), yncscape(104.588 + 4.762, 4.115)), module, PwmClock::BPM_INC));
	addParam(createParam<DNSWITCH>(Vec(mm2px(14.452), yncscape(99.788 + 4.762, 4.115)), module, PwmClock::BPM_DEC));

	SigDisplayWidget *display = new SigDisplayWidget(3);
	display->box.pos = Vec(mm2px(22), RACK_GRID_HEIGHT - mm2px(108 + 4.762));
	display->box.size = Vec(30 + 14, 20);
	if(module != NULL)
		display->value = &module->bpm;
	addChild(display);

	addChild(createParam<BefacoPushBig>(Vec(mm2px(2.937), yncscape(109.841, 8.999)), module, PwmClock::PULSE));
	addInput(createInput<PJ301BPort>(Vec(mm2px(3.309), yncscape(99.175, 8.255)), module, PwmClock::PULSE_IN));

	ParamWidget *pw = createParam<Davies1900hFixWhiteKnob>(Vec(mm2px(49.602), yncscape(99.483 + 4.762, 9.525)), module, PwmClock::BPM);
	((Davies1900hKnob *)pw)->snap = true;
	addParam(pw);
	addInput(createInput<PJ301BPort>(Vec(mm2px(63.162f), yncscape(104.880, 8.255)), module, PwmClock::EXT_BPM));
	addInput(createInput<PJ301YPort>(Vec(mm2px(35.392f), yncscape(86.857, 8.255)), module, PwmClock::RESET));

	addParam(createParam<NKK1>(Vec(mm2px(7.769), yncscape(87.34, 9.488)), module, PwmClock::OFFON));
	addChild(createLight<SmallLight<RedLight>>(Vec(mm2px(3.539), yncscape(89.897, 2.176)), module, PwmClock::ACTIVE));
	addInput(createInput<PJ301BPort>(Vec(mm2px(21.633), yncscape(86.857, 8.255)), module, PwmClock::REMOTE_IN));

	
	addParam(createParam<Davies1900hFixRedKnob>(Vec(mm2px(48.511), yncscape(40.132, 9.525)), module, PwmClock::SWING));
	addInput(createInput<PJ301BPort>(Vec(mm2px(63.162), yncscape(40.767, 8.255)), module, PwmClock::SWING_IN));

	addParam(createParam<Davies1900hFixBlackKnob>(Vec(mm2px(48.511), yncscape(24.482, 9.525)), module, PwmClock::PWM));
	addInput(createInput<PJ301BPort>(Vec(mm2px(63.162), yncscape(25.117, 8.255)), module, PwmClock::PWM_IN));

	float col_x[3] = {7.875, 21.633, 35.392};
	float pos_y = yncscape(70.175, 8.255);
	int col = 0;
	for(int k = 0; k < OUT_SOCKETS; k++)
	{
		addOutput(createOutput<PJ301OPort>(Vec(mm2px(col_x[col]), pos_y), module, PwmClock::OUT_1 + k));
		if(++col >= 3)
		{
			col = 0;
			pos_y += mm2px(11);
		}
	}
	addOutput(createOutput<PJ301BLUPort>(Vec(mm2px(49.145), yncscape(10.525, 8.255)), module, PwmClock::ONSTOP));
}

void PwmClockWidget::SetBpm(float bpm)
{
	int index = getParamIndex(PwmClock::BPM);
	if(index >= 0)
	{
		// VCV interface update is ahem.... migliorabile....
		Davies1900hFixWhiteKnob *pKnob = (Davies1900hFixWhiteKnob *)params[index];
		bool smooth = pKnob->smooth;
		pKnob->smooth = false;
		params[index]->paramQuantity->setValue(bpm);
		pKnob->smooth = smooth;
	}
}
