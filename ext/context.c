#include <debase_internals.h>

static VALUE cContext;
static int thnum_current = 0;

static VALUE idAlive;

static inline VALUE
Context_thnum(VALUE self) {
  debug_context_t *context;
  Data_Get_Struct(self, debug_context_t, context);
  return INT2FIX(context->thnum);
}

inline void
delete_frame(debug_context_t *context)
{
  debug_frame_t *frame;

  frame = context->stack;
  context->stack = frame->prev;
  context->stack_size--;
  xfree(frame);
}

inline void
init_frame(debug_frame_t *frame, char* file, int line, VALUE binding, VALUE self)
{ 
  frame->file = file;
  frame->line = line;
  frame->binding = binding;
  frame->self = self;  
}

inline static VALUE 
Context_stack_size(VALUE self) 
{
  debug_context_t *context;
  Data_Get_Struct(self, debug_context_t, context);
  return INT2FIX(context->stack_size);
}

inline static VALUE 
Context_thread(VALUE self) 
{
  debug_context_t *context;
  Data_Get_Struct(self, debug_context_t, context);
  return context->thread;
}

inline static VALUE 
Context_dead(VALUE self) 
{
  debug_context_t *context;
  Data_Get_Struct(self, debug_context_t, context);
  return IS_THREAD_ALIVE(context->thread) ? Qfalse : Qtrue;
}

extern VALUE 
Context_ignored(VALUE self) 
{
  debug_context_t *context;

  if (self == Qnil) return Qtrue;
  Data_Get_Struct(self, debug_context_t, context);
  return CTX_FL_TEST(context, CTX_FL_IGNORE) ? Qtrue : Qfalse;
}

extern void  
push_frame(VALUE context_object, char* file, int line, VALUE binding, VALUE self) 
{
  debug_context_t *context;
  debug_frame_t *frame;
  Data_Get_Struct(context_object, debug_context_t, context);

  frame = ALLOC(debug_frame_t);
  init_frame(frame, file, line, binding, self);
  frame->prev = context->stack;
  context->stack = frame;
  context->stack_size++;
}

extern void
pop_frame(VALUE context_object) 
{
  debug_context_t *context;
  Data_Get_Struct(context_object, debug_context_t, context);

  if (context->stack_size > 0) {
    delete_frame(context);
  }
}

extern void
update_frame(VALUE context_object, char* file, int line, VALUE binding, VALUE self) 
{
  debug_context_t *context;
  Data_Get_Struct(context_object, debug_context_t, context);

  if (context->stack_size == 0) {
  	push_frame(context_object, file, line, binding, self);
    return;
  }
  init_frame(context->stack, file, line, binding, self);
}

static void 
Context_mark(debug_context_t *context) 
{
  debug_frame_t *frame;

  rb_gc_mark(context->thread);
  frame = context->stack;
  while (frame != NULL) {
    rb_gc_mark(frame->self);
    rb_gc_mark(frame->binding);
    frame = frame->prev;
  }
}

static void
Context_free(debug_context_t *context) {
  while(context->stack_size > 0) {
    delete_frame(context);
  }
  xfree(context);
}

extern VALUE
context_create(VALUE thread, VALUE cDebugThread) {
  debug_context_t *context;

  context = ALLOC(debug_context_t);
  context->stack_size = 0;
  context->stack = NULL;
  context->thnum = ++thnum_current;
  context->thread = thread;
  context->flags = 0;
  if(rb_obj_class(thread) == cDebugThread) CTX_FL_SET(context, CTX_FL_IGNORE);
  return Data_Wrap_Struct(cContext, Context_mark, Context_free, context);
}

static inline void
check_frame_number_valid(debug_context_t *context, int frame_no)
{
  if (frame_no < 0 || frame_no >= context->stack_size) {
    rb_raise(rb_eArgError, "Invalid frame number %d, stack (0...%d)",
        frame_no, context->stack_size);
  }
}

static debug_frame_t*
get_frame_no(debug_context_t *context, int frame_n)
{
  debug_frame_t *frame;
  int i;

  check_frame_number_valid(context, frame_n);
  frame = context->stack;
  for (i = 0; i < frame_n; i++) {
    frame = frame->prev;
  }
  return frame;
}

static VALUE
Context_frame_file(int argc, VALUE *argv, VALUE self)
{
  debug_context_t *context;
  debug_frame_t *frame;
  VALUE frame_no;
  int frame_n;

  Data_Get_Struct(self, debug_context_t, context);
  frame_n = rb_scan_args(argc, argv, "01", &frame_no) == 0 ? 0 : FIX2INT(frame_no);
  frame = get_frame_no(context, frame_n);
  return rb_str_new2(frame->file);
}

static VALUE
Context_frame_line(int argc, VALUE *argv, VALUE self)
{
  debug_context_t *context;
  debug_frame_t *frame;
  VALUE frame_no;
  int frame_n;

  Data_Get_Struct(self, debug_context_t, context);
  frame_n = rb_scan_args(argc, argv, "01", &frame_no) == 0 ? 0 : FIX2INT(frame_no);
  frame = get_frame_no(context, frame_n);
  return INT2FIX(frame->line);
}

static VALUE
Context_frame_binding(int argc, VALUE *argv, VALUE self)
{
  debug_context_t *context;
  debug_frame_t *frame;
  VALUE frame_no;
  int frame_n;

  Data_Get_Struct(self, debug_context_t, context);
  frame_n = rb_scan_args(argc, argv, "01", &frame_no) == 0 ? 0 : FIX2INT(frame_no);
  frame = get_frame_no(context, frame_n);
  return frame->binding;
}

static VALUE
Context_frame_self(int argc, VALUE *argv, VALUE self)
{
  debug_context_t *context;
  debug_frame_t *frame;
  VALUE frame_no;
  int frame_n;

  Data_Get_Struct(self, debug_context_t, context);
  frame_n = rb_scan_args(argc, argv, "01", &frame_no) == 0 ? 0 : FIX2INT(frame_no);
  frame = get_frame_no(context, frame_n);
  return frame->self;
}

static VALUE
Context_stop_reason(VALUE self)
{
    debug_context_t *context;
    const char *symbol;

    Data_Get_Struct(self, debug_context_t, context);
    
    switch(context->stop_reason)
    {
        case CTX_STOP_STEP:
            symbol = "step";
            break;
        case CTX_STOP_BREAKPOINT:
            symbol = "breakpoint";
            break;
        case CTX_STOP_CATCHPOINT:
            symbol = "catchpoint";
            break;
        case CTX_STOP_NONE:
        default:
            symbol = "none";
    }
    if(CTX_FL_TEST(context, CTX_FL_DEAD))
        symbol = "post-mortem";
    
    return ID2SYM(rb_intern(symbol));
}

/*
 *   Document-class: Context
 *
 *   == Summary
 *
 *   Debugger keeps a single instance of this class for each Ruby thread.
 */
VALUE
Init_context(VALUE mDebase)
{
  cContext = rb_define_class_under(mDebase, "Context", rb_cObject);
  rb_define_method(cContext, "stack_size", Context_stack_size, 0);
  rb_define_method(cContext, "thread", Context_thread, 0);
  rb_define_method(cContext, "dead?", Context_dead, 0);
  rb_define_method(cContext, "ignored?", Context_ignored, 0);
  rb_define_method(cContext, "thnum", Context_thnum, 0);
  rb_define_method(cContext, "stop_reason", Context_stop_reason, 0);
  rb_define_method(cContext, "frame_file", Context_frame_file, -1);
  rb_define_method(cContext, "frame_line", Context_frame_line, -1);
  rb_define_method(cContext, "frame_binding", Context_frame_binding, -1);
  rb_define_method(cContext, "frame_self", Context_frame_self, -1);

  idAlive = rb_intern("alive?");

  return cContext;
    // rb_define_method(cContext, "stop_next=", context_stop_next, -1);
    // rb_define_method(cContext, "step", context_stop_next, -1);
    // rb_define_method(cContext, "step_over", context_step_over, -1);
    // rb_define_method(cContext, "stop_frame=", context_stop_frame, 1);
    // rb_define_method(cContext, "suspend", context_suspend, 0);
    // rb_define_method(cContext, "suspended?", context_is_suspended, 0);
    // rb_define_method(cContext, "resume", context_resume, 0);
    // rb_define_method(cContext, "tracing", context_tracing, 0);
    // rb_define_method(cContext, "tracing=", context_set_tracing, 1);
    // rb_define_method(cContext, "ignored?", context_ignored, 0);
    // rb_define_method(cContext, "frame_args", context_frame_args, -1);
    // rb_define_method(cContext, "frame_args_info", context_frame_args_info, -1);
    // rb_define_method(cContext, "frame_class", context_frame_class, -1);
    // rb_define_method(cContext, "frame_id", context_frame_id, -1);
    // rb_define_method(cContext, "frame_locals", context_frame_locals, -1);
    // rb_define_method(cContext, "frame_method", context_frame_id, -1);
    // rb_define_method(cContext, "breakpoint", 
    //          context_breakpoint, 0);      /* in breakpoint.c */
    // rb_define_method(cContext, "set_breakpoint", 
    //          context_set_breakpoint, -1); /* in breakpoint.c */
    // rb_define_method(cContext, "jump", context_jump, 2);
    // rb_define_method(cContext, "pause", context_pause, 0);
}