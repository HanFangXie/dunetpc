art_make( NO_PLUGINS
          
          LIB_LIBRARIES
                        lardata_Utilities
			larcorealg_Geometry
                        ${MF_MESSAGELOGGER}
                        
                        cetlib cetlib_except
            ${ART_FRAMEWORK_CORE}
			    ${ART_FRAMEWORK_PRINCIPAL}
			    ${ART_FRAMEWORK_SERVICES_REGISTRY}
			    ${ART_FRAMEWORK_SERVICES_OPTIONAL}
			    ${ART_FRAMEWORK_SERVICES_OPTIONAL_RANDOMNUMBERGENERATOR_SERVICE}
			    ${ART_FRAMEWORK_SERVICES_OPTIONAL_TFILESERVICE_SERVICE}
			    art_Persistency_Common canvas
			    art_Persistency_Provenance canvas
			    art_Utilities canvas
			${FHICLCPP}
                        ${ROOT_BASIC_LIB_LIST}
			${Boost_SYSTEM_LIBRARY}
        )



install_headers()
install_fhicl()
install_source()
