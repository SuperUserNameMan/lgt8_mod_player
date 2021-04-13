#ifndef MOD_SAMPLES_MAX
	#define MOD_SAMPLES_MAX 15
#endif

#ifndef MOD_CHANNELS_MAX
	#define MOD_CHANNELS_MAX 4 
#endif

#ifdef __cplusplus
	#define FALLTHROUGH [[fallthrough]]
#else
	#define FALLTHROUGH
#endif

#ifdef MOD_DIRECT_PGM
	#define MOD_PGM( p, a ) p[a]
#else
	#define MOD_PGM( p, a ) pgm_read_byte( p + a )
#endif


//#define MOD_FX_EE_PATTERN_DELAY

typedef struct
{
	const volatile int8_t *data_pgm;
	uint16_t data_len;
	
} mod_sample;

typedef struct
{
	uint8_t sample_id;
	
	uint8_t volume;
	uint16_t period;
	uint16_t target_period;
	uint16_t delayed_period;
	
	float position;
	float increment;
	
	uint8_t fx;
	uint8_t fx_val;
	uint8_t fx_val_0x3;
	uint8_t fx_val_0x4;
	uint8_t fx_val_0x7;
	uint8_t fx_val_0x9;
	
	uint8_t fx_val_0xE1;
	uint8_t fx_val_0xE2;
	uint8_t fx_val_0xEA;
	uint8_t fx_val_0xEB;
	
	uint8_t fx_0xE6_loop_count;
	uint8_t fx_0xE6_loop_line;
	
	struct 
	{
		uint8_t update_volume : 1;
		uint8_t update_pitch  : 1;
	};
	
	uint8_t real_volume;
	
} mod_chan;

typedef struct
{
	const volatile uint8_t *source_pgm;
	uint16_t source_len;
	
	mod_sample samples[MOD_SAMPLES_MAX];
	uint8_t  n_samples;
		
	const volatile uint8_t *order_pgm;
	uint8_t  order_len;
	uint8_t  order_reset;
	uint8_t  order_pos;
	
	const volatile uint8_t *patterns_data_pgm;
	uint16_t patterns_data_len; // in bytes
	 int8_t  pattern_line;
	uint8_t  pattern_line_tick;
	uint8_t  pattern_delay;
	uint8_t  n_patterns;
	
	uint8_t  ticks_per_line;
	
	mod_chan  channels[MOD_CHANNELS_MAX];
	uint8_t n_channels;
	
	uint16_t max_sps; // max samples per seconds
	float samples_per_tick;
	float sample_pos;
	
	int err;
	
} mod_ctx;

#define MOD_MIN( a, b ) ((a) < (b) ? (a) : (b))
#define MOD_MAX( a, b ) ((a) > (b) ? (a) : (b))

#define MOD_SAMPLE_INFO_ADR( sample_id, param ) ( 20 + 30*(sample_id) + (param) )
#define MOD_SAMPLE_INFO_PARAM_NAME        0
#define MOD_SAMPLE_INFO_PARAM_LENGTH      ( MOD_SAMPLE_INFO_PARAM_NAME + 22 )
#define MOD_SAMPLE_INFO_PARAM_FINETUNE    ( MOD_SAMPLE_INFO_PARAM_LENGTH + 2 )
#define MOD_SAMPLE_INFO_PARAM_VOLUME      ( MOD_SAMPLE_INFO_PARAM_FINETUNE + 1 )
#define MOD_SAMPLE_INFO_PARAM_LOOP_START  ( MOD_SAMPLE_INFO_PARAM_VOLUME + 1 )
#define MOD_SAMPLE_INFO_PARAM_LOOP_LENGTH ( MOD_SAMPLE_INFO_PARAM_LOOP_START + 2 )

void mod_update_volume( mod_ctx *ctx, mod_chan *chan )
{
	// TODO : fx 0x07
	
	chan->real_volume = MOD_MAX(0, MOD_MIN( 64, chan->volume ) );
	chan->update_volume = 0;
}


const float _mod_arpeggio[16] = { /* 2^(X/12) for X in 0..15 */
	1.000000f, 1.059463f, 1.122462f, 1.189207f,
	1.259921f, 1.334840f, 1.414214f, 1.498307f,
	1.587401f, 1.681793f, 1.781797f, 1.887749f,
	2.000000f, 2.118926f, 2.244924f, 2.378414f
};

void mod_update_pitch( mod_ctx *ctx, mod_chan *chan )
{
	chan->increment = 0;
	
	if ( chan->period )
	{
		float period = chan->period;
		
		
		if ( chan->fx == 0x4 || chan->fx == 0x6 )
		{
			// TODO : fx vibrato : 0x04 0x06
		}
		else 
		if ( chan->fx == 0x0 && chan->fx_val ) 
		{
			int step = (chan->fx_val >> ((2 - ctx->pattern_line_tick % 3) << 2)) & 0x0f;
			period /= _mod_arpeggio[ step ];
		}
		
		chan->increment = 3546894.6f / ( period * (float)ctx->max_sps );
	}
	
	chan->update_pitch = 0;
}

void mod_volume_slide( mod_chan *chan, uint8_t fx_val )
{
	int8_t v = ( ( fx_val & 0xf0 ) ? ( fx_val >> 4 ) & 0x0f : -( fx_val & 0x0f ) ) + chan->volume;
	
	chan->volume = MOD_MAX( 0, MOD_MIN( 64, v ) );
	
	chan->update_volume = 1;
}

void mod_pitch_slide( mod_chan *chan, int8_t fx_val )
{
	// TODO : clamp by taking finetune into account
	
	chan->period += fx_val;
	chan->period = MOD_MAX( chan->period, 113 );
	chan->period = MOD_MIN( chan->period, 856 );
	chan->update_pitch = 1;
}

/* Memorize a parameter unless the new value is zero */
#define MOD_MEM_NZ_OCTET( dst, src ) (dst) = (src) ? (src) : (dst);

/* Same thing, but memorize each nibble separately */
#define MOD_MEM_NZ_QUARTETS( dst, src ) {\
		(dst) = (((src) & 0x0f) ? ((src) & 0x0f) : ((dst) & 0x0f)) \
		      | (((src) & 0xf0) ? ((src) & 0xf0) : ((dst) & 0xf0)); \
	}

void mod_next_line( mod_ctx *ctx )
{
	int pattern_break = -1;
	
#ifdef MOD_DEBUG_ORDER_POS
	ctx->order_pos=MOD_DEBUG_ORDER_POS; // DEBUG
#endif
	
	ctx->pattern_line++; 
	
	if ( ctx->pattern_line >= 64 )
	{
		ctx->order_pos++;
		
		if ( ctx->order_pos == ctx->order_len )
		{
			ctx->order_pos = ctx->order_reset;
		}
#ifdef MOD_DEBUG
		Serial.print(F("---"));
		Serial.print(ctx->order_pos);
		Serial.println(F("---"));
#endif
		ctx->pattern_line = 0;
	}

#ifdef MOD_DIRECT_PGM
	const uint8_t (*data_pgm)[4] = (uint8_t(*)[4])( ctx->patterns_data_pgm + ( MOD_PGM( ctx->order_pgm, ctx->order_pos ) * 64 + ctx->pattern_line ) * ctx->n_channels * 4 );
#else
	const uint8_t (*data_pgm)[4] = (uint8_t(*)[4])( ctx->patterns_data_pgm + ( MOD_PGM( ctx->order_pgm, ctx->order_pos ) * 64 + ctx->pattern_line ) * ctx->n_channels * 4 );
#endif
	
#ifdef MOD_DEBUG_CHAN
	int i = MOD_DEBUG_CHAN;
#else
	for( int i = 0; i < ctx->n_channels; i++ )
#endif
	{
		mod_chan *chan = &ctx->channels[ i ];

		int sample = (  MOD_PGM(data_pgm[i], 0 ) & 0xf0        ) | ( MOD_PGM(data_pgm[i], 2 ) >> 4);
		int period = (( MOD_PGM(data_pgm[i], 0 ) & 0x0f ) << 8 ) |   MOD_PGM(data_pgm[i], 1 );
		int effect = (( MOD_PGM(data_pgm[i], 2 ) & 0x0f ) << 8 ) |   MOD_PGM(data_pgm[i], 3 );
		
#ifdef MOD_DEBUG
		Serial.print( sample );
		Serial.print(':');
		Serial.print( period );
		Serial.print(':');
		Serial.print( effect, HEX);
		Serial.print('|');
#endif

		// decode effect
		
		if ( ( effect >> 8 ) == 0x0e )
		{
			chan->fx     = effect >> 4;
			chan->fx_val = effect & 0x0f;
		}
		else
		{
			chan->fx     = effect >> 8;
			chan->fx_val = effect & 0xff;
		}
		
		if ( sample > 0 )
		{
			if ( sample <= MOD_SAMPLES_MAX )
			{
				chan->sample_id = sample;
				//chan->finetune  = MOD_PGM( ctx->source_pgm, MOD_SAMPLE_INFO_ADR( sample-1, MOD_SAMPLE_INFO_PARAM_FINETUNE ) ] ) & 0x0f;

				chan->volume    = MOD_MIN( 64, MOD_PGM( ctx->source_pgm, MOD_SAMPLE_INFO_ADR( sample-1, MOD_SAMPLE_INFO_PARAM_VOLUME ) ) );
				
				if (chan->fx != 0xED) 
				{
					chan->update_volume = 1;
				}
			}
			else
			{
				chan->sample_id = 0;
			}
		}
		
		if ( period > 0 )
		{
			// TODO : add finetune to period
			
			if (chan->fx != 0x3) {
				if (chan->fx == 0xED) 
				{
					chan->delayed_period = period;
				}
				else
				{
					chan->period = period;
					chan->position = 0;
					//chan->lfo_step = 0; // TODO
					
					chan->update_pitch = 1;
				} 
			}
		}
		
		// handle pattern FX
		switch( chan->fx )
		{
			case 0x3: 
				MOD_MEM_NZ_OCTET( chan->fx_val_0x3, chan->fx_val );
			FALLTHROUGH;
			case 0x5: 
				MOD_MEM_NZ_OCTET( chan->target_period, period );
			break;
			
			case 0x4: 
				MOD_MEM_NZ_QUARTETS( chan->fx_val_0x4, chan->fx_val );
			break;
			
			case 0x7: 
				MOD_MEM_NZ_QUARTETS( chan->fx_val_0x7, chan->fx_val );
			break;
			
			case 0xE1 : 
				MOD_MEM_NZ_OCTET( chan->fx_val_0xE1, chan->fx_val );
			break;
			
			case 0xE2 : 
				MOD_MEM_NZ_OCTET( chan->fx_val_0xE2, chan->fx_val );
			break;
			
			case 0xEA : 
				MOD_MEM_NZ_OCTET( chan->fx_val_0xEA, chan->fx_val );
			break;
			
			case 0xEB :
				MOD_MEM_NZ_OCTET( chan->fx_val_0xEB, chan->fx_val );
			break;
			
			// TODO 8xx stereo balance

			case 0x9: // 9xx sample offset
				if ( period != 0 || sample != 0 ) 
				{
					MOD_MEM_NZ_OCTET( chan->fx_val_0x9, chan->fx_val );
					chan->position = chan->fx_val_0x9 << 8;
				}
			break;
			
			case 0xB: // Bxx: Jump to pattern
				ctx->order_pos = chan->fx_val < ctx->order_len ? chan->fx_val : 0;
				ctx->pattern_line = -1;
			break;
			
			case 0xC: // Cxx set volume
				chan->volume = MOD_MAX(0, MOD_MIN( 64, chan->fx_val ));
				chan->update_volume = 1;
			break;
			
			case 0xD: // Dxy pattern break
				pattern_break = ( chan->fx_val >> 4 ) * 10 + ( chan->fx_val & 0x0f );
			break;
			
			// TODO E4x set vibrator waveform
			
			case 0xE5 : // E5x set sample finetune
				// TODO
				chan->update_pitch = 1;
			break;
			
			case 0xE6: // E6x pattern loop
				if ( chan->fx_val ) 
				{
					if ( ! chan->fx_0xE6_loop_count ) 
					{
						chan->fx_0xE6_loop_count = chan->fx_val;
						ctx->pattern_line = chan->fx_0xE6_loop_line;
					} 
					else
					{
						chan->fx_0xE6_loop_count--;
						if ( chan->fx_0xE6_loop_count )
						{
							ctx->pattern_line = chan->fx_0xE6_loop_line;
						}
					}
				}
				else 
				{
					chan->fx_0xE6_loop_line = ctx->pattern_line - 1;
				}
			break;
			
			// TODO E7x set tremolo waveform
			// TODO E8x set stereo balance
			
			case 0xEE: // EEx pattern delay
				ctx->pattern_delay = chan->fx_val;
			break;
			
			case 0xF: // Fxx set speed
			{
				if ( chan->fx_val != 0 )
				{
					if ( chan->fx_val < 32 )
					{
						ctx->ticks_per_line = chan->fx_val;
					}
					else
					{
						ctx->samples_per_tick = ctx->max_sps / ( 0.4f * (float)chan->fx_val );
					}
				}
			}
			break;
			
			default:break;
		}
	}
	
#ifdef MOD_DEBUG
	Serial.println();
#endif
	
	// handle pattern break FX Dxy here
	if ( pattern_break != -1 ) 
	{
		ctx->pattern_line = ( pattern_break < 64 ? pattern_break : 0 ) - 1;
		
		ctx->order_pos++;
		
		if ( ctx->order_pos >= ctx->order_len ) 
		{
			ctx->order_pos = ctx->order_reset;
		}
	}
	
	
}

void mod_next_tick( mod_ctx *ctx )
{
	
	ctx->pattern_line_tick++;
	
	if ( ctx->pattern_line_tick >= ctx->ticks_per_line )
	{
		// EEx pattern delay
		if ( ctx->pattern_delay > 0 )
		{
			ctx->pattern_delay--;
		}
		else
		{
			mod_next_line( ctx );
		}
		
		ctx->pattern_line_tick = 0;
	}
	
#ifdef MOD_DEBUG_CHAN
	int i = MOD_DEBUG_CHAN;
#else
	for( int i = 0; i < ctx->n_channels; i++ )
#endif
	{
		mod_chan *chan = &ctx->channels[ i ];
		
		// every tick channel FX
		switch( chan->fx )
		{
			case 0x0 : // 0xy : Arpeggio
			{
				chan->update_pitch = 1;
			}
			break;
			
			case 0xE9: // E9x : retrigger note every x ticks
				if ( ! ( chan->fx_val && ( ctx->pattern_line_tick % chan->fx_val ) ) ) 
				{
					chan->position = 0;
					//chan->lfo_step = 0;
				}
			break;
			
			case 0xEC: // ECx : cut note after x ticks
				if ( ctx->pattern_line_tick == chan->fx_val ) 
				{
					chan->volume = 0;
					chan->update_volume = 1;
				}
			break;
			
			case 0xED: // EDx : delay note for x ticks
				if ( ctx->pattern_line_tick == chan->fx_val && chan->sample_id ) {
					chan->update_pitch = 1;
					chan->update_volume = 1;
					chan->period = chan->delayed_period;
					chan->position = 0;
					//chan->lfo_step = 0;
				}
			break;
			
			default:break;
		}
		
		// first tick channel FX
		if ( ctx->pattern_line_tick == 0 )
		{
			switch( chan->fx )
			{
				case 0xE1: // E1 : Fineslide up
					mod_pitch_slide(chan, -chan->fx_val_0xE1); 
				break;
				case 0xE2: // E2 : Fineslide down
					mod_pitch_slide(chan, chan->fx_val_0xE2); 
				break;
				case 0xEA: // EA : Fine volume slide up
					mod_volume_slide(chan, chan->fx_val_0xEA << 4); 
				break;
				case 0xEB: // EB : Fine volume slide down
					mod_volume_slide(chan, chan->fx_val_0xEB & 0x0f); 
				break;
				
				default:
				break;
			}
		}
		// next ticks channel FX
		else
		{
			switch( chan->fx )
			{
				case 0x1 : // 1xx : slide note up (portamento)
					mod_pitch_slide( chan, -chan->fx_val );
				break;
				
				case 0x2 : // 2xx : slide note down (portamento)
					mod_pitch_slide( chan,  chan->fx_val );
				break;
				
				case 0x5 : // 5xy : vol slide + continue note slide
					mod_volume_slide( chan, chan->fx_val );
				FALLTHROUGH;
				
				case 0x3 : // 3xx : tone slide (portamento)
				{
					if ( chan->period < chan->target_period )
					{
						chan->period += chan->fx_val_0x3;
						if ( chan->period > chan->target_period ) 
						{
							chan->period = chan->target_period;
						}
					}
					else
					{
						chan->period -= chan->fx_val_0x3;
						if ( chan->period < chan->target_period )
						{
							chan->period = chan->target_period;
						}
					}
					chan->update_pitch = 1;
					
					//~ int rate = chan->fx_val_0x3;
					//~ int order = chan->period < chan->target_period;
					//~ int closer = chan->period + (order ? rate : -rate);
					//~ int new_order = closer < chan->target_period;
					//~ chan->period = new_order == order ? closer : chan->target_period;
					//~ chan->update_pitch = 1;
				}
				break;
				
				case 0x06 : // 6xy : vol slide + continue vibrato
					mod_volume_slide( chan, chan->fx_val );
				FALLTHROUGH;
				
				case 0x04 : // 0x4 : vibrato (oscillate picth)
					// TODO vibrato
					chan->update_pitch = 1;
				break;
				
				case 0x07 : // 0x7 : tremolo (oscillate volume)
					// TODO tremolo
					chan->update_volume = 1;
				break;
				
				case 0xA : // Axy : volume slide
					mod_volume_slide( chan, chan->fx_val );
				break;
				
				default:break;
			}
		}
		
		if ( chan->update_volume ) 
		{
			mod_update_volume( ctx, chan );
		}
		
		if ( chan->update_pitch )
		{
			mod_update_pitch( ctx, chan );
		}
	}
	
	
}

int16_t mod_render_channel( mod_ctx *ctx, mod_chan *chan, uint32_t dt )
{
	if ( chan->sample_id == 0 ) return 0;
	
	mod_sample *sample = &ctx->samples[ chan->sample_id - 1 ];
	
	uint16_t loop_start =
		( MOD_PGM( ctx->source_pgm, MOD_SAMPLE_INFO_ADR( chan->sample_id-1, MOD_SAMPLE_INFO_PARAM_LOOP_START ) + 0 ) << 9 )
		+
		( MOD_PGM( ctx->source_pgm, MOD_SAMPLE_INFO_ADR( chan->sample_id-1, MOD_SAMPLE_INFO_PARAM_LOOP_START ) + 1 ) << 1 )
		;
		
	uint16_t loop_length =
		( MOD_PGM( ctx->source_pgm, MOD_SAMPLE_INFO_ADR( chan->sample_id-1, MOD_SAMPLE_INFO_PARAM_LOOP_LENGTH ) + 0 ) << 9 )
		+
		( MOD_PGM( ctx->source_pgm, MOD_SAMPLE_INFO_ADR( chan->sample_id-1, MOD_SAMPLE_INFO_PARAM_LOOP_LENGTH ) + 1 ) << 1 )
		;
	
	uint16_t loop_end = loop_length > 2 ? loop_start + loop_length : 0;
	
#ifdef MOD_DEBUG
	//~ Serial.println( chan->sample_id );
	//~ Serial.println( (uint16_t)sample->data_pgm - (uint16_t)ctx->source_pgm );
	//~ Serial.println( loop_start );
	//~ Serial.println( loop_length);
	//~ Serial.println( loop_end );
	//~ ctx->err = __LINE__;
#endif
	
	int volume = chan->real_volume;
	
	int8_t s = 0;
	if ( (uint16_t)chan->position < sample->data_len )
	{
		s = MOD_PGM( sample->data_pgm, MOD_MIN( (uint16_t)chan->position, sample->data_len ) );
		chan->position += chan->increment;
	}
	
	if ( loop_end && chan->position >= loop_end )
	{
		chan->position -= loop_length;
	}
	else
	if ( chan->position >= sample->data_len )
	{
		chan->position = -1;
	}
	
	return (int16_t)volume*s;
}

int16_t mod_render_sample( mod_ctx *ctx, uint32_t dt )
{
	if ( ctx->err ) return 0;
	
	int16_t out = 0;

#ifdef MOD_DEBUG_CHAN
	int i = MOD_DEBUG_CHAN;
#else
	for( int i = 0; i < ctx->n_channels ; i++ )
#endif
	{
		mod_chan *chan = &ctx->channels[ i ];
		
#ifdef MOD_DEBUG
		//~ Serial.print(i);
		//~ Serial.print(' ');
		//~ Serial.print(chan->period);
		//~ Serial.print(' ');
		//~ Serial.print(chan->position);
		//~ Serial.print(' ');
		//~ Serial.print(chan->sample_id);
		//~ Serial.println();
#endif
		
		if ( chan->period > 0 && chan->position >= 0 && chan->sample_id > 0 )
		{
			out += mod_render_channel( ctx, chan, dt );
		}
	}
	
#ifdef MOD_DEBUG
	//Serial.println( out );
#endif
	
	ctx->sample_pos += 1;
	
	if ( ctx->sample_pos >= ctx->samples_per_tick )
	{
		ctx->sample_pos -= ctx->samples_per_tick;
		
		mod_next_tick( ctx );
		
		if ( ctx->pattern_line == 0 && ctx->pattern_line_tick == 0 )
		{
			// TODO : STOP 
		}
	}
	
	return out;
}

char* mod_tag( mod_ctx *ctx, char* str )
{
	for( auto i=0; i<4; i++ )
	{
		str[ i ] = MOD_PGM( ctx->source_pgm, 1080 + i );
	}
	str[ 4 ] = 0;
	
	return str;
}

int mod_tag_to_channels( mod_ctx *ctx )
{
	char tag[5];
	
	mod_tag( ctx, tag );
	
	if ( tag[0] == 'M' && tag[1] == '.' && tag[2] == 'K' && tag[3] == '.' ) return 4;
	if ( tag[0] == 'M' && tag[1] == '!' && tag[2] == 'K' && tag[3] == '!' ) return 4;
	
	if ( tag[0] == 'F' && tag[1] == 'L' && tag[2] == 'T' && tag[3] == '4' ) return 4;
	
	if ( tag[0] == '1' && tag[1] == 'C' && tag[2] == 'H' && tag[3] == 'N' ) return 1;
	if ( tag[0] == '2' && tag[1] == 'C' && tag[2] == 'H' && tag[3] == 'N' ) return 2;
	if ( tag[0] == '3' && tag[1] == 'C' && tag[2] == 'H' && tag[3] == 'N' ) return 3;
	if ( tag[0] == '4' && tag[1] == 'C' && tag[2] == 'H' && tag[3] == 'N' ) return 4;

	return 0; // not supported
}



int mod_identification( mod_ctx *ctx )
{
	// mod with 31 instruments ?
	if ( ctx->source_len >= 1084 )
	{
		ctx->n_channels = mod_tag_to_channels( ctx );
		
		if ( ctx->n_channels == 0 ) return __LINE__;

		ctx->order_len     =  MOD_PGM( ctx->source_pgm, 950 );
		ctx->order_reset   =  MOD_PGM( ctx->source_pgm, 951 ) >= ctx->order_len ? 0 : MOD_PGM( ctx->source_pgm, 951 );
		ctx->order_pgm         =  ctx->source_pgm + 952;
		ctx->patterns_data_pgm =  ctx->source_pgm + 1084;
	
		ctx->n_samples     = MOD_MIN( MOD_SAMPLES_MAX, 31 );
		
		return 0;
	}
	
#ifdef MOD_DEBUG
	// mod with 15 instruments are longer than 600 bytes
	if ( ctx->source_len < 600 )
	{
		return __LINE__;
	}
	
	// song title must be ASCII and 0
	for( int i=0; i<20; i++ ) 
	{
		char c = (char)MOD_PGM( ctx->source_pgm, i );
		
		if ( c != 0 && ( c < ' ' || c > '~' ) )
		{
			return __LINE__;
		}
	}
	
	// samples names must be in ASCII and 0
	for( int i=0; i<15; i++ )
	{
		for( int j=0; j<22; j++ )
		{
			char c = (char)MOD_PGM( ctx->source_pgm, MOD_SAMPLE_INFO_ADR( i, MOD_SAMPLE_INFO_PARAM_NAME ) + j );
		
			if ( c != 0 && ( c < ' ' || c > '~' ) )
			{
				return __LINE__;
			}
		}
	}
#endif
	
	// seems valid 15 instruments mod

	ctx->order_len     =  MOD_PGM( ctx->source_pgm, 470 );
	ctx->order_reset   =  MOD_PGM( ctx->source_pgm, 471 ) >= ctx->order_len ? 0 : MOD_PGM( ctx->source_pgm, 471 );
	ctx->order_pgm         = ctx->source_pgm + 472;
	ctx->patterns_data_pgm = ctx->source_pgm + 600;
	
	ctx->n_samples     = MOD_MIN( MOD_SAMPLES_MAX, 15 );
	ctx->n_channels    = 4;
	
	return 0;
}

int mod_init( mod_ctx *ctx, const volatile uint8_t *source_pgm, uint16_t source_len, uint16_t max_sps )
{
	memset( ctx, 0, sizeof(mod_ctx) );

#ifdef MOD_DIRECT_PGM
	ctx->source_pgm     = (uint8_t*)((uint16_t)source_pgm + 0x4000);
#else
	ctx->source_pgm     = source_pgm;
#endif
	ctx->source_len = source_len;
	
	int res = 0;
	
	res = mod_identification( ctx );
	if ( res != 0 )
	{
		ctx->err = res;
		return res;
	}
		
	// count the actuall number of patterns used in the order list
	
	ctx->n_patterns = 0;
	
	for( int i=0; ( i < 128 ) && ( MOD_PGM( ctx->order_pgm, i ) < 128 ) ; i++ )
	{
		ctx->n_patterns = MOD_MAX( ctx->n_patterns, MOD_PGM( ctx->order_pgm, i ) );
	}
	
	ctx->n_patterns++;
	
	ctx->patterns_data_len = 256 * ctx->n_channels * ctx->n_patterns;
	
	// initilize the samples
	
	const volatile int8_t *samples_data_pgm = (int8_t*)( (uint16_t)ctx->patterns_data_pgm + ctx->patterns_data_len );
		
	for( int i=0; i < ctx->n_samples; i++ )
	{
		uint16_t len = 
			( MOD_PGM( ctx->source_pgm, MOD_SAMPLE_INFO_ADR( i, MOD_SAMPLE_INFO_PARAM_LENGTH ) + 0 ) << 8 )
			|
			( MOD_PGM( ctx->source_pgm, MOD_SAMPLE_INFO_ADR( i, MOD_SAMPLE_INFO_PARAM_LENGTH ) + 1 ) << 1 )
			;			
		ctx->samples[ i ].data_len = len > 2 ? len : 0;
		ctx->samples[ i ].data_pgm = samples_data_pgm;
		samples_data_pgm += ctx->samples[ i ].data_len;
	}
	
	// default setrtings
	
	ctx->ticks_per_line    = 6;
	ctx->pattern_line      = -1;
	ctx->pattern_line_tick = 6-1;
	ctx->samples_per_tick  = max_sps / 50.0f;
	ctx->max_sps           = max_sps;
	
	mod_next_tick( ctx );
	
	return 0;
}
