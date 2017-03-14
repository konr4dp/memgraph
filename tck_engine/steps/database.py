def query(q, context, params={}):
    """
    Function used to execute query on database. Query results are 
    set in context.result_list. If exception occurs, it is set on
    context.exception.

    @param q: 
        String, database query.
    @param context:
        behave.runner.Context, context of all tests.
    @return:
        List of query results.
    """
    driver = context.driver
    results_list = []

    if context.config.database == "neo4j":
        session = driver.session()
        try:
            #executing query
            with session.begin_transaction() as tx:
                results = tx.run(q, params)
                summary = results.summary()
                if not context.config.no_side_effects:
                    add_side_effects(context, summary.counters)
                results_list = list(results)
                tx.success = True
        except Exception as e:
            #exception
            print(e)
            context.exception = e
            context.log.info('%s', str(e))
            #not working if removed
            query("match (n) detach delete(n)", context)
        finally:
            session.close()
    return results_list


def add_side_effects(context, counters):
    """
    Funtion adds side effects from query to graph properties.

    @param context:
        behave.runner.Context, context of all tests.
    """
    graph_properties = context.graph_properties
    
    #check nodes
    if counters.nodes_created > 0:
        graph_properties.change_nodes(counters.nodes_created)
    if counters.nodes_deleted > 0:
        graph_properties.change_nodes(-counters.nodes_deleted)
    #check relationships
    if counters.relationships_created > 0:
        graph_properties.change_relationships(counters.relationships_created)
    if counters.relationships_deleted > 0:
        graph_properties.change_relationships(-counters.relationships_deleted)
    #check labels
    if counters.labels_added > 0:
        graph_properties.change_labels(counters.labels_added)
    if counters.labels_removed > 0:
        graph_properties.change_labels(-counters.labels_removed)
    #check properties
    if counters.properties_set > 0:
        graph_properties.change_properties(counters.properties_set)
    
