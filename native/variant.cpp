#include "stdafx.h"
#include "com4j.h"
#include "com4j_variant.h"
#include "xducer.h"
#include "unmarshaller.h"

// used to map two jlongs to the memory image of VARIANT.
static VARIANT* getVariantImage( JNIEnv* env, jobject buffer ) {
	return reinterpret_cast<VARIANT*>(env->GetDirectBufferAddress(buffer));
}


JNIEXPORT void JNICALL Java_com4j_Variant_clear0(JNIEnv* env, jclass, jobject image) {
	HRESULT hr = VariantClear(getVariantImage(env,image));
	if(FAILED(hr))
		error(env,hr,"failed to clear variant");
}

// change the variant type in the same place
void VariantChangeType( JNIEnv* env, VARIANT* v, VARTYPE type ) {
	VARIANT dst;
	VariantInit(&dst);
	HRESULT hr = VariantChangeType(&dst,v,0, type );
	if(FAILED(hr)) {
		error(env,hr,"failed to change the variant type");
		return;
	}
	VariantClear(v);
	*v = dst;
}

JNIEXPORT void JNICALL Java_com4j_Variant_changeType0(JNIEnv* env, jclass, jint type, jobject image) {
	VariantChangeType( env, getVariantImage(env,image), (VARTYPE)type );
}







class VariantHandler {
public:
	// returnss VARIANT allocated by 'new'
	virtual VARIANT* set( JNIEnv* env, jobject src ) = 0;
	virtual jobject get( JNIEnv* env, VARIANT* v ) = 0;
};

template <VARTYPE vt, class XDUCER>
class VariantHandlerImpl : public VariantHandler {
protected:
	inline XDUCER::NativeType& addr(VARIANT* v) {
		return *reinterpret_cast<XDUCER::NativeType*>(&v->boolVal);
	}
public:
	VARIANT* set( JNIEnv* env, jobject o ) {
		VARIANT* v = new VARIANT();
		VariantInit(v);
		v->vt = vt;
		addr(v) = XDUCER::toNative(
			env, static_cast<XDUCER::JavaType>(o) );
		return v;
	}
	jobject get( JNIEnv* env, VARIANT* v ) {
		VARIANT dst;
		VariantInit(&dst);
		VariantChangeType(&dst,v,0,vt);
		jobject o = XDUCER::toJava(env, addr(v));
		VariantClear(&dst);
		return o;
	}
};

class ComObjectVariandHandlerImpl : public VariantHandlerImpl<VT_DISPATCH,xducer::Com4jObjectXducer> {
	VARIANT* set( JNIEnv* env, jobject o) {
		VARIANT* v = ComObjectVariandHandlerImpl::set(env,o);
		IDispatch* pDisp = NULL;
		HRESULT hr = addr(v)->QueryInterface(&pDisp);
		if(SUCCEEDED(hr)) {
			// if possible, use VT_DISPATCH.
			addr(v)->Release();
			addr(v) = pDisp;
			v->vt = VT_DISPATCH;
		} // otherwise use VT_UNKNOWN. See java.net issue 2.
		return v;
	}
};

struct SetterEntry {
	JClassID* cls;
	VariantHandler* handler;
};

static SetterEntry setters[] = {
	{ &javaLangBoolean,		new VariantHandlerImpl<VT_BOOL,		xducer::BoxedVariantBoolXducer>() },
	{ &javaLangString,		new VariantHandlerImpl<VT_BSTR,		xducer::StringXducer>() },
	{ &javaLangFloat,		new VariantHandlerImpl<VT_R4,		xducer::BoxedFloatXducer>() },
	{ &javaLangDouble,		new VariantHandlerImpl<VT_R8,		xducer::BoxedDoubleXducer>() },
	{ &javaLangShort,		new VariantHandlerImpl<VT_I2,		xducer::BoxedShortXducer>() },
	{ &javaLangInteger,		new VariantHandlerImpl<VT_I4,		xducer::BoxedIntXducer>() },
	{ &javaLangLong,		new VariantHandlerImpl<VT_I8,		xducer::BoxedLongXducer>() },
	// see issue 2 on java.net. I used to convert a COM object to VT_UNKNOWN
	{ &com4j_Com4jObject,	new VariantHandlerImpl<VT_DISPATCH,	xducer::Com4jObjectXducer>() },
	{ NULL,					NULL }
};

// convert a java object to a VARIANT based on the actual type of the Java object.
// the caller should call VariantClear to clean up the data, then delete it.
//
// return NULL if fails to convert
VARIANT* convertToVariant( JNIEnv* env, jobject o ) {
	jclass cls = env->GetObjectClass(o);
	
	for( SetterEntry* p = setters; p->cls!=NULL; p++ ) {
		if( env->IsAssignableFrom( cls, *(p->cls) ) ) {
			VARIANT* v = p->handler->set(env,o);
			return v;
		}
	}

	return NULL;
}

jobject VariantUnmarshaller::unmarshal( JNIEnv* env ) {
	for( SetterEntry* p = setters; p->cls!=NULL; p++ ) {
		if( env->IsAssignableFrom( retType, *(p->cls) ) ) {
			return p->handler->get(env,&v);
		}
	}
	// the expected return type is something we can't handle
	error(env,"The specified return type is not compatible with VARIANT");
	return NULL;
}
