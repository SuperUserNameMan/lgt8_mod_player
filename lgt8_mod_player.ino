#pragma GCC optimize("Ofast")
//~ #pragma GCC optimize("O3")
//~ #pragma GCC optimize("O2")
//~ #pragma GCC optimize("O1")
//~ #pragma GCC optimize("O0")

volatile const PROGMEM
#include "tunes/tune.h" // OK ? (first channel is missing sometimes ?)
//~ #include "tunes/tune_1.h" // random freeze, crashes
//~ #include "tunes/tune_for_nothing.h" // OK ?
//~ #include "tunes/tune_free.h" // OK ?
//~ #include "tunes/tune_it_in.h" // OK ?
//~ #include "tunes/tune_me_in_sucka_.h" // crash
//~ #include "tunes/tune-o-matic_3.h" // OK ?
//~ #include "tunes/tune-o-matic_5.h" // OK ?


// ---------------------------------------------------------------------

//~ #define MOD_DEBUG
//~ #define MOD_DEBUG_ORDER_POS 1
//~ #define MOD_DEBUG_CHAN 1
#define MOD_DIRECT_PGM
//~ #define MOD_PWM_AUDIO 3 // works on Atmega328p, but not on LGT8Fx yet

#ifndef __LGT8F__
	#undef MOD_DIRECT_PGM
	#ifndef MOD_PWM_AUDIO
		#define MOD_PWM_AUDIO 3 
	#endif
#endif

#include "mod_player.h"

// ---------------------------------------------------------------------

#define AUDIO_SPS ( F_CPU / 2560 )

//#define AUDIO_SPS 20000
#define AUDIO_DT  ( 1000000L / AUDIO_SPS )

uint32_t t0 = 0;
uint32_t dt = 0;

mod_ctx ctx;

void setup()
{
	pinMode( LED_BUILTIN, OUTPUT );
	digitalWrite( LED_BUILTIN, HIGH );
	
	delay(1000);
	
#ifdef MOD_PWM_AUDIO
	pinMode( MOD_PWM_AUDIO, OUTPUT );
	TCCR2B = TCCR2B & B11111000 | B00000001; // for PWM frequency of 31372.55 Hz
#else
	analogReference(DEFAULT);
	pinMode(DAC0, ANALOG);
#endif
	
	Serial.begin(115200);
	
	int err = mod_init( &ctx, tune_mod, tune_mod_len, AUDIO_SPS );
	if ( err )
	{
		Serial.print(F("mod_init() : Err ")); Serial.println( err );
	}
	
#ifdef MOD_DEBUG
	char tag[5];
	
	Serial.print(F("Mod title : "));
		#ifdef MOD_DIRECT_PGM
			Serial.println( (char*)ctx.source_pgm );
		#else
			char buffer[32]; strcpy_P( buffer, ctx.source_pgm );
			Serial.println( buffer );
		#endif
	Serial.print(F("mod tag   : ")); Serial.println( mod_tag( &ctx, tag ) );
	
	Serial.print(F("song len  : ")); Serial.println( ctx.order_len );
	Serial.print(F("patterns  : ")); Serial.println( ctx.n_patterns );
	Serial.print(F("song rst  : ")); Serial.println( ctx.order_reset );
	Serial.print(F("pat. data : 0x")); Serial.print( (uint16_t)ctx.patterns_data_pgm, HEX ); Serial.print(F("[")); Serial.print( ctx.patterns_data_len );Serial.println(F("]"));
	
	Serial.print(F("n samples : ")); Serial.println( ctx.n_samples );
	
	for( int i=0; i<ctx.n_samples; i++ )
	{
		Serial.print(F("Sample["));
		Serial.print(i);
		Serial.print(F("]=0x")); // address of the sample into the .MOD
		Serial.print((uint16_t)ctx.samples[i].data_pgm,HEX); 
		Serial.print(F("[")); // length of the sample 
		Serial.print((uint16_t)ctx.samples[i].data_len, HEX);
		Serial.println(F("]"));
	}
	
	Serial.print(F("n channels : ")); Serial.println( ctx.n_channels );
	
	for( int i=0; i<ctx.n_channels; i++ )
	{
		Serial.print(F("Channel["));
		Serial.print(i);
		Serial.print(F("]={ sample_id="));
		Serial.print(ctx.channels[i].sample_id);
		Serial.print(F("; volume="));
		Serial.print(ctx.channels[i].volume);
		Serial.print(F("; period="));
		Serial.print(ctx.channels[i].period);
		Serial.print(F("; position="));
		Serial.print(ctx.channels[i].position);
		Serial.print(F("; increment="));
		Serial.print(ctx.channels[i].increment);
		Serial.println(F("; }"));
	}
	
	//ctx.err = 1; // lock
	
	Serial.flush();
#endif

	t0 = micros();
}



void loop()
{
	if ( ctx.err == 0)
	{
		int32_t sample = mod_render_sample( &ctx, dt ) ;
		
		sample = sample / 256 + 128;
#ifdef MOD_DEBUG
	if ( sample < 0 || sample > 255 )
	{
		Serial.print(F("Sample overflow :"));
		Serial.print( sample );
		Serial.println();
		//ctx.err = 1;
	}
#endif
		//Serial.println( sample );

#ifdef MOD_PWM_AUDIO
		analogWrite( MOD_PWM_AUDIO, sample );
#else
		DALR = (uint8_t)( sample );
		//DALR = (uint8_t)( ( sample / 64 ) + 128 );
#endif	
		digitalWrite( LED_BUILTIN, !(ctx.pattern_line % 4) );
		
		while( (micros()-t0) < AUDIO_DT );
		t0 = micros();
	}
	else
	{
		digitalWrite( LED_BUILTIN, HIGH );
		delay(100);
		
		digitalWrite( LED_BUILTIN, LOW );
		delay(100);
		
	}
}


