/* -----------------------------------------------------------------------------
 * See the LICENSE file for information on copyright, usage and redistribution
 * of SWIG, and the README file for authors - http://www.swig.org/release.html.
 *
 * scilab.cxx
 *
 * Scilab language module for SWIG.
 * ----------------------------------------------------------------------------- */

char cvsroot_scilab_cxx[] = "$Id$";

#include "swigmod.h"

static const char *usage = (char *) "\
Scilab Options (available with -scilab)\n\
     (none yet)\n\n";


class SCILAB:public Language {
 private:
  File *f_begin;
  File *f_runtime;
  File *f_header;
  File *f_wrappers;
  File *f_init;
  File *f_builder;

 public:
   SCILAB():f_begin(0), f_runtime(0), f_header(0),f_wrappers(0),
	    f_init(0) {}
    
   
  /* ------------------------------------------------------------
   * main()
   * ------------------------------------------------------------ */
  
  virtual void main(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
      if (argv[i]) {
	if (strcmp(argv[i], "-help") == 0) {
	  fputs(usage, stderr);
	}
      }
    }
    
    //Set language-specific subdirectory in SWIG library 
    SWIG_library_directory("scilab");
   
    // Add a symbol to the parser for conditional compilation
    Preprocessor_define("SWIGSCILAB 1", 0);
    
    // Set scilab configuration file
    SWIG_config_file("scilab.swg");
    
     //Set typemap for scilab
    SWIG_typemap_lang("scilab");
  }

  /* ---------------------------------------------------------------------
   * top()
   * --------------------------------------------------------------------- */

  virtual int top(Node *n) {
    
    Node *mod = Getattr(n, "module");
     
    /*Get the name of the module*/ 
    String *module = Getattr(n, "name");
    
    /*One output file for as the wrapper file*/
    String *outfile = Getattr(n, "outfile");
    f_begin = NewFile(outfile, "w", SWIG_output_files());
    
    /*Another output file to generate the .so or .dll */
    String *builder = NewString("builder.sce");
    f_builder=NewFile(builder,"w",SWIG_output_files());
    
    /* Initialize all of the output files */
    if (!f_begin) {
      FileErrorDisplay(outfile);
      SWIG_exit(EXIT_FAILURE);
     }
    f_runtime = NewString("");
    f_header = NewString("");
    f_wrappers = NewString("");
    f_init = NewString("");
    
    /* Register file targets with the SWIG file handler */
    Swig_register_filebyname("begin", f_begin);
    Swig_register_filebyname("runtime", f_runtime);
    Swig_register_filebyname("header", f_header);
    Swig_register_filebyname("wrapper", f_wrappers);
    Swig_register_filebyname("init", f_init);
     
    /*Insert the banner at the beginning */
    Swig_banner(f_begin);

    /*Include some header file of scilab*/
    Printf(f_runtime, "#include \"stack-c.h\"\n");
    Printf(f_runtime, "#include \"sciprint.h\"\n");
    Printf(f_runtime, "#include \"Scierror.h\"\n");
    
    /*Initialize the builder.sce file*/
    Printf(f_builder,"ilib_name = \"%slib\";\n",module);
    Printf(f_builder,"files = [\"%s\",\"%s.o\"];\n", outfile,module);
    Printf(f_builder,"libs = [];\n");
    Printf(f_builder, "table = ["); 
    
    
    /*Emit code for children*/
    Language::top(n);
    
    /*Finish off the builder.sce file*/
    Printf(f_builder,"];\n");
    Printf(f_builder,"ilib_build(ilib_name,table,files,libs);");
    
    /*Dump out all the files*/
    Dump(f_runtime, f_begin);
    Dump(f_header, f_begin);
    Dump(f_wrappers, f_begin);
    Wrapper_pretty_print(f_init, f_begin);
    
    /* Close all of the files */
    Delete(f_init);
    Delete(f_wrappers);
    Delete(f_header);
    Delete(f_runtime);
    Close(f_begin);
    Close(f_builder);
    Delete(f_begin);
    Delete(f_builder);

    return SWIG_OK;
  }

   /* ----------------------------------------------------------------------
   * functionWrapper()
   * ---------------------------------------------------------------------- */

  virtual int functionWrapper(Node *n) {
    
    // A new wrapper function object
    Wrapper *f = NewWrapper();
    
    Parm *p;
    String *tm;
    int j;

    //Get the useful information from the node
    String *nodeType = Getattr(n, "nodeType");
    int constructor = (!Cmp(nodeType, "constructor"));
    int destructor = (!Cmp(nodeType, "destructor"));
    String *storage = Getattr(n, "storage");

    bool overloaded = !!Getattr(n, "sym:overloaded");
    bool last_overload = overloaded && !Getattr(n, "sym:nextSibling");
    String *iname = Getattr(n, "sym:name");
    String *wname = Swig_name_wrapper(iname);
    String *overname = Copy(wname);
    SwigType *d = Getattr(n, "type");
    ParmList *l = Getattr(n, "parms");

    if (!overloaded && !addSymbol(iname, n))
      return SWIG_ERROR;

    if (overloaded)
      Append(overname, Getattr(n, "sym:overname"));

    Printv(f->def, "int ", overname, " (char *fname){", NIL);
   
   // Emit all of the local variables for holding arguments
    emit_parameter_variables(l, f);
    
    //Attach typemaps to the parameter list
    emit_attach_parmmaps(l, f);
    Setattr(n, "wrap:parms", l);
     
    // Get number of required and total arguments
    int num_arguments = emit_num_arguments(l);
    int num_required = emit_num_required(l);
    int varargs = emit_isvarargs(l);
   
    if (constructor && num_arguments == 1 && num_required == 1) {
      if (Cmp(storage, "explicit") == 0) {
	Node *parent = Swig_methodclass(n);
	if (GetFlag(parent, "feature:implicitconv")) {
	  String *desc = NewStringf("SWIGTYPE%s", SwigType_manglestr(Getattr(n, "type")));
	  Printf(f->code, "if (SWIG_CheckImplicit(%s)) SWIG_fail;\n", desc);
	  Delete(desc);
	}
      }
    }
  
   //Walk the function parameter list and generate code to get arguments
    for (j = 0, p = l; j < num_arguments; ++j) {
      while (checkAttribute(p, "tmap:in:numinputs", "0")) {
	p = Getattr(p, "tmap:in:next");
      }

      SwigType *pt = Getattr(p, "type");

      // Get typemap for this argument
      String *tm = Getattr(p, "tmap:in");
       
      if (tm) {
	if (!tm || checkAttribute(p, "tmap:in:numinputs", "0")) {
	  p = nextSibling(p);
	  continue;
	}
       String *getargs = NewString("");
       Printv(getargs, tm, NIL);
	Printv(f->code, getargs, "\n", NIL);
	Delete(getargs);
       p = Getattr(p, "tmap:in:next");
	continue;
      } else {
	Swig_warning(WARN_TYPEMAP_IN_UNDEF, input_file, line_number, "Unable to use type %s as a function argument.\n", SwigType_str(pt, 0));
	break;
      }
    }
    
    Setattr(n, "wrap:name", overname);   
   
   // Now write code to make the function call
    Swig_director_emit_dynamic_cast(n, f);
    String *actioncode = emit_action(n);
    
   //Insert the return variable 
    emit_return_variable(n, d, f);

   if ((tm = Swig_typemap_lookup_out("out", n, "result", f, actioncode))) {

      Printf(f->code, "%s\n", tm);
     
    } 
   else {
   Swig_warning(WARN_TYPEMAP_OUT_UNDEF, input_file, line_number, "Unable to use return type %s in function %s.\n", SwigType_str(d, 0), iname);
    }
    
    /* Insert argument output code */
    String *outarg = NewString("");
    for (p = l; p;) {
      if ((tm = Getattr(p, "tmap:argout"))) {
	Replaceall(tm, "$result", "_outp");
	//Replaceall(tm, "$arg", Getattr(p, "emit:input"));
	//Replaceall(tm, "$input", Getattr(p, "emit:input"));
	Printv(outarg, tm, "\n", NIL);
	p = Getattr(p, "tmap:argout:next");
      } else {
	p = nextSibling(p);
      }
    }
    Printv(f->code, outarg, NIL);
   
    /* Finish the the code for the function  */
    Printf(f->code, "return 0;\n");	
    Printf(f->code, "}\n");

    Replaceall(f->code, "$symname", iname);
    
    /* Dump the wrapper function */
    Wrapper_print(f, f_wrappers);
    DelWrapper(f);
    Printf(f_builder, "\"%s\",\"%s\";",iname,wname);

    Delete(overname);
    Delete(wname);
    Delete(outarg);
  
    return SWIG_OK;
  }

  /* -----------------------------------------------------------------------
   * variableWrapper()
   * ----------------------------------------------------------------------- */

  virtual int variableWrapper(Node *n) {
 
    Language::variableWrapper(n);	/* Default to functions */

    return SWIG_OK;
  }

};

extern "C" Language *swig_scilab(void) {
  return new SCILAB();
}