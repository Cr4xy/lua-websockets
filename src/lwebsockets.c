#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "libwebsockets.h"
#include <string.h>
#include <assert.h>

#define WS_META "ws.meta"
#define MAX_PROTOCOLS 4
#define MAX_EXTENSIONS 4
#define MAX_USERDATA 5

struct lws_link {
  void *userdata;
  int protocol_index;
};

struct lws_userdata {
  lua_State *L;
  int protocol_function_refs[MAX_PROTOCOLS];
  struct libwebsocket_context *ws_context;
  int destroyed;
  int protocol_count;
  int index;  
  char protocol_names[MAX_PROTOCOLS][100];
  struct libwebsocket_protocols protocols[MAX_PROTOCOLS];
  struct libwebsocket_extension extensions[MAX_EXTENSIONS];
  struct lws_link links[MAX_PROTOCOLS];
};

static struct lws_userdata* lws_userdatas[MAX_USERDATA];

static struct lws_userdata *lws_create_userdata(lua_State *L) {
  struct lws_userdata *user = lua_newuserdata(L, sizeof(struct lws_userdata));;
  user->L = L;
  //  user->tableref = LUA_REFNIL;
  user->ws_context = NULL;
  user->destroyed = 0;
  user->protocol_count = 0;
  user->index = -1;
  memset(user->protocols,0,sizeof(struct libwebsocket_protocols)*MAX_PROTOCOLS);
  memset(user->extensions,0,sizeof(struct libwebsocket_extension)*MAX_EXTENSIONS);
  return user;
}

static void lws_delete_userdata(lua_State *L, struct lws_userdata *lws_user) {
  //  lua_unref(L, lws_user->tableref);
  //  lws_user->tableref = LUA_REFNIL;
}

static int lws_callback(struct libwebsocket_context * context,
			struct libwebsocket *wsi,
			 enum libwebsocket_callback_reasons reason, void *session,
			void *in, size_t len, void *user) {
  struct lws_link* link = user;
  struct lws_userdata* lws_user = link->userdata;
  int argc = 1;
  lua_rawgeti(lws_user->L, LUA_REGISTRYINDEX, lws_user->protocol_function_refs[link->protocol_index]);
  lua_pushnumber(lws_user->L,reason);
  if( len > 0 && in != NULL ) {
    lua_pushlstring(lws_user->L,in,len);
    ++argc;
  }
  lua_call(lws_user->L,argc,0);
  //  printf("call %s %d\n",name,	lua_isfunction(lws_userdatas[index]->L,1));	

}


static int lws_context(lua_State *L) {
  int port = 0;
  const char* interf = NULL;
  const char* ssl_cert_filepath = NULL;
  const char* ssl_private_key_filepath = NULL;
  int gid = -1;
  int uid = -1;
  unsigned int options = 0;
  struct lws_userdata *user = lws_create_userdata(L);
  int index = 0;

  luaL_getmetatable(L, WS_META);
  lua_setmetatable(L, -2);

  if( lua_type(L, 1) == LUA_TTABLE ) {
    lua_getfield(L, 1, "port");
    port = luaL_optint(L, -1, 0);    
    lua_pop(L, 1);

    lua_getfield(L, 1, "interf");
    interf = luaL_optstring(L, -1, NULL);    
    lua_pop(L, 1);

    lua_getfield(L, 1, "protocols");    
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushvalue(L, 1);
    assert(lua_setfenv(L, -3) == 1);
    //    user->tableref = luaL_ref(L, 1);

    lua_pushnil(L);
    while(user->protocol_count < MAX_PROTOCOLS && lua_next(L, -2) != 0) {  
      int n = user->protocol_count;
      strcpy(user->protocol_names[n],luaL_checkstring(L,-2));
      user->protocols[n].name = user->protocol_names[n];
      user->protocols[n].callback = lws_callback;
      user->protocols[n].per_session_data_size = 0;
      lua_pushvalue(L, -1);
      user->protocol_function_refs[n] = luaL_ref(L, LUA_REGISTRYINDEX);
      lua_remove(L, 1);
      ++user->protocol_count;
      lua_pop(L, 1);
      user->links[n].userdata = user;
      user->links[n].protocol_index = n;
      user->protocols[n].user = &user->links[n];
    }
    lua_pop(L, 1);
  }  
  while(lws_userdatas[index] != NULL) {
    ++index;
    if(index > MAX_USERDATA) {
      luaL_error(L, "websockets: out of userdata");
    }    
  }
  lws_userdatas[index] = user;
  user->index = index;

  user->ws_context = libwebsocket_create_context(port, interf, user->protocols, user->extensions, ssl_cert_filepath, ssl_private_key_filepath, gid, uid, options);
  return 1;
}

static int lws_destroy(lua_State *L) {  
  struct lws_userdata *user = (struct lws_userdata *)luaL_checkudata(L, 1, WS_META);
  if(!user->destroyed) {
    if(user->ws_context != NULL) {
      libwebsocket_context_destroy(user->ws_context);
    }
    luaL_argcheck(L, user, 1, "websocket context expected");
    lws_delete_userdata(L, user);
    user->destroyed = 1;
  }
  if(user->index > -1) {
    lws_userdatas[user->index] = NULL;
  }
  return 0;
}

static const struct luaL_Reg lws_module_methods [] = {
  {"context",lws_context},
  {NULL,NULL}
};

static const struct luaL_Reg lws_context_methods [] = {
  {"destroy",lws_destroy},
  {"__gc",lws_destroy},
  {NULL,NULL}
};

int luaopen_websockets(lua_State *L) {
  memset(lws_userdatas, 0 ,sizeof(struct lws_userdata*)*MAX_USERDATA);
  luaL_newmetatable(L, WS_META);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  luaL_register(L, NULL, lws_context_methods);
  luaL_register(L, "websockets", lws_module_methods);
  return 1;
}
