/* The base OpenAL module simply provides both the ALC and AL modules. It's
 * intended to provide just the core/standard functionality, without any
 * extensions.
 */

export module openal;

export import openal.alc;
export import openal.al;
